#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
#include <format>
#include <algorithm>
#include <set>
#include <limits>

// Windows用ヘッダはGLFWよりも上に配置
#define NOMINMAX // Windows.h の min/max マクロ定義を抑止する
#include <windows.h>
#include <commdlg.h>

// GLFW / OpenGL
#include <GLFW/glfw3.h>

// ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "i2c_device.hpp"
#include "json.hpp"

// nlohmann/json のエイリアス
using json = nlohmann::json;

namespace {
    I2CDeviceManager manager;
    double max_timestamp = 0.0; // データの最大時刻を保持
    double min_timestamp = std::numeric_limits<double>::max(); // データの最小時刻を保持
    std::vector<double> timestamps; // 全てのユニークなタイムスタンプ（1ステップずつの移動用）
    std::vector<uint8_t> address_list; // ログに出てきたデバイスだけ画面に出す用

    bool first_layout = true;
}

// Windowsのファイル選択ダイアログを開いてパスを取得する関数
std::wstring OpenFileDialog() {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = L"CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return L"";
}

// devicesフォルダ内にある全ての JSON ファイルからデバイス設定を読み込んで登録する関数
bool loadDeviceConfig(I2CDeviceManager& manager) {
    std::filesystem::path dir_path = "devices";
    
    // devicesディレクトリが存在するか確認
    if (!std::filesystem::exists(dir_path) || !std::filesystem::is_directory(dir_path)) {
        std::cerr << "Config directory not found: " << dir_path.string() << std::endl;
        return false;
    }

    bool loaded_any = false;

    // フォルダ内のファイルを走査
    for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            std::ifstream file(entry.path());
            if (!file.is_open()) {
                std::cerr << "Failed to open config file: " << entry.path().string() << std::endl;
                continue;
            }

    try {
        json j;
        file >> j;

        if (j.contains("devices") && j["devices"].is_array()) {
            for (const auto& dev : j["devices"]) {
                I2CDevice::Config config;
                config.device_name = dev.value("name", "UNKNOWN");
                std::string dev_addr_7bit_str = dev.value("dev_addr_7bit", "0x00");
                config.i2c_dev_addr = static_cast<uint8_t>(std::stoul(dev_addr_7bit_str, nullptr, 16));        
                config.reg_addr_bytes = dev.value("reg_addr_bytes", 1);
                config.auto_addr_inc = dev.value("auto_addr_inc", true);
                config.support_direct_read_from_default_reg = dev.value("support_direct_read_from_default_reg", false);
                std::string default_reg_addr_str = dev.value("default_reg_addr", "0x00");
                config.default_reg_addr = static_cast<uint32_t>(std::stoul(default_reg_addr_str, nullptr, 16)); 
                config.reg_map_size = dev.value("reg_map_size", 256); // デフォルトサイズ
                std::string endian_str = dev.value("endian", "little");
                config.is_little_endian = (endian_str == "little");
                auto device = manager.RegisterDevice(config);

                // コマンドID定義の読み込みと登録
                if (dev.contains("registers") && dev["registers"].is_array()) {
                    for (const auto& reg : dev["registers"]) {
                        std::string reg_addr_str = reg.value("address", "0x00");
                        std::string reg_name = reg.value("name", "UNKNOWN");

                        uint32_t reg_addr = static_cast<uint32_t>(std::stoul(reg_addr_str, nullptr, 16));
                        device->SetRegisterName(reg_addr, reg_name);

                        // ビット配列をパースして登録するラムダ式
                        auto parse_and_register = [&](const std::string& key, auto register_func) {
                            if (reg.contains(key) && reg[key].is_array()) {
                                for (const auto& bit : reg[key]) {
                                    I2CDevice::BitFieldInfo bitfield;
                                    bitfield.name = bit.value("name", "UNKNOWN");
                                    bitfield.byte_offset = bit.value("byte_offset", 0);
                                    bitfield.bit_offset = bit.value("bit_offset", 0);
                                    bitfield.bit_width = bit.value("width", 1);
                                    bitfield.is_little_endian = config.is_little_endian;
                                    
                                    // 指定された登録用メソッドを呼び出す（is_little_endian を渡す）
                                    (device->*register_func)(reg_addr, bitfield);
                                }
                            }
                        };

                        // 1. 共通の bits は「リード」と「ライト」の両方に登録する
                        if (reg.contains("bits") && reg["bits"].is_array()) {
                            parse_and_register("bits", &I2CDevice::SetReadRegisterBitField);
                            parse_and_register("bits", &I2CDevice::SetWriteRegisterBitField);
                        }

                        // 2. リード専用/個別 bits の読み込み
                        parse_and_register("bits_read", &I2CDevice::SetReadRegisterBitField);

                        // 3. ライト専用/個別 bits の読み込み
                        parse_and_register("bits_write", &I2CDevice::SetWriteRegisterBitField);
                    }
                }
            }
        }
                loaded_any = true;
    } catch (const std::exception& e) {
                std::cerr << "JSON Parse Error in " << entry.path().string() << ": " << e.what() << std::endl;
            }
        }
    }

    if (!loaded_any) {
        std::cerr << "No valid JSON config files found in 'devices' directory." << std::endl;
        return false;
    }

    return true;
}

// CSV解析用関数（タイムスタンプを収集・ソートする）
void processCSV(I2CDeviceManager& manager, const std::wstring& filename) {
    std::filesystem::path p(filename);
    if (!std::filesystem::exists(p)) return;

    std::ifstream file(p);
    if (!file.is_open()) return;

    std::string line;
    std::getline(file, line); // ヘッダー読み飛ばし

    std::set<double> unique_timestamps;
    I2CDevice* crnt_device = nullptr;

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string name, type, ack, addr_str, read_str, data_str;
        double timestamp, duration;

        std::getline(ss, name, ',');
        std::getline(ss, type, ',');
        ss >> timestamp; ss.ignore();
        ss >> duration; ss.ignore();
        std::getline(ss, ack, ',');
        std::getline(ss, addr_str, ',');
        std::getline(ss, read_str, ',');
        std::getline(ss, data_str, ',');

        if (type == "\"address\"") {
            uint8_t addr_7bit = static_cast<uint8_t>(std::stoul(addr_str, nullptr, 16));
            bool exists = (std::find(address_list.begin(), address_list.end(), addr_7bit) != address_list.end());
            if (!exists) {
                address_list.push_back(addr_7bit);
            }

            bool is_read = (read_str == "true");
            crnt_device = manager.GetDevice(addr_7bit);    
            // JSON設定に登録されていないI2Cアドレスの場合は、
            // デフォルト設定のデバイスとして動的登録する
            if (crnt_device == nullptr) {
                I2CDevice::Config default_config;
                default_config.device_name = std::format("0x{:02X}", addr_7bit);
                default_config.i2c_dev_addr = addr_7bit;
                default_config.reg_addr_bytes = 1;
                default_config.auto_addr_inc = true;
                default_config.support_direct_read_from_default_reg = false;
                default_config.default_reg_addr = 0x00;
                default_config.reg_map_size = 256;

                crnt_device = manager.RegisterDevice(default_config);

                std::cout
                    << "Unregistered I2C device detected. Registered automatically: "
                    << default_config.device_name
                    << ", address bytes=" << default_config.reg_addr_bytes
                    << ", auto_addr_inc=" << default_config.auto_addr_inc
                    << std::endl;
            }
            if (crnt_device != nullptr){
                crnt_device->CallThisEachI2CAddrByteWrite(is_read);
            }
        } else if (type == "\"data\"") {
            if (crnt_device != nullptr) {
                uint8_t data = static_cast<uint8_t>(std::stoul(data_str, nullptr, 16));
                crnt_device->DataByte(data, timestamp);
            }
        } else if (type == "\"stop\"") {
            for (auto& [addr, device_ptr] : manager.GetAllDevices()){
                device_ptr->CallThisEachI2CStopCondition();
            }
        }
    }

    // タイムスタンプを収集
    // address byteのタイムスタンプを除外
    // data byteでもアドレスの書き込みのdataは除外
    // このためデバイス内部のタイムスタンプを全捜査して取得する
    for (auto& [addr, device_ptr] : manager.GetAllDevices()) {
        for (auto& timestamp : device_ptr->GetAllTimestamp()){
            unique_timestamps.insert(timestamp);
            max_timestamp = std::max(max_timestamp, timestamp);
            min_timestamp = std::min(min_timestamp, timestamp);
        }
    }

    // std::set から vector へコピーしてソート済みのタイムスタンプリストを作成
    timestamps.assign(unique_timestamps.begin(), unique_timestamps.end());
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 1. GLFW初期化
    if (!glfwInit()) return -1;
    GLFWwindow* window = glfwCreateWindow(1145, 1040, "I2C_MemoryMap", NULL, NULL);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // VSync有効

    // 2. ImGui初期化
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    
    
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // iniファイルの自動保存を無効化
    
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // 3. devices.json の自動読み込み
    if (!loadDeviceConfig(manager)) {
        MessageBoxW(NULL, L"Failed to load JSON files from the 'devices' directory.", L"Error", MB_OK | MB_ICONERROR);
        glfwTerminate();
        return 0;
    }

    // ダイアログでCSVファイルを選択させる
    std::wstring csv_path = OpenFileDialog();
    if (!csv_path.empty()) {
        processCSV(manager, csv_path);

        // ★ フルパスをウィンドウタイトルに追加して更新する
        // 1. std::wstring から std::string (UTF-8) に変換
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, csv_path.c_str(), (int)csv_path.length(), NULL, 0, NULL, NULL);
        std::string utf8_path(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, csv_path.c_str(), (int)csv_path.length(), &utf8_path[0], size_needed, NULL, NULL);

        // 2. タイトル文字列を組み立ててGLFWに設定
        std::string new_title = std::format("I2C_MemoryMap - [{}]", utf8_path);
        glfwSetWindowTitle(window, new_title.c_str());
    } else {
        return 0;
    }

    // 表示対象の時刻設定（インデックス、または直接の時刻）
    static double target_time = min_timestamp;

    // 4. メインループ
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // ImGuiフレーム開始
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // --- キーボード入力による前後の1データ移動処理 ---
        if (!timestamps.empty()) {
            // 現在の target_time に最も近いタイムスタンプのインデックスを検索
            auto it = std::lower_bound(timestamps.begin(), timestamps.end(), target_time);
            size_t idx = std::distance(timestamps.begin(), it);
            if (idx >= timestamps.size()) idx = timestamps.size() - 1;

            // フォーカスが他の入力欄に奪われていない、かつウィンドウがアクティブな時などに反応させたい場合は ImGui::IsWindowFocused 等を使えますが、
            // ここではシンプルに矢印キーの入力を監視します。
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
                if (idx > 0) {
                    idx--;
                    target_time = timestamps[idx];
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
                if (idx + 1 < timestamps.size()) {
                    idx++;
                    target_time = timestamps[idx];
                }
            }
        }

        // メインウィンドウのサイズや位置を取得
        ImVec2 main_pos = ImGui::GetMainViewport()->Pos;
        ImVec2 main_size = ImGui::GetMainViewport()->Size;

        // タイムライン制御用コントロールパネル（画面下部）
        float control_panel_height = 100.0f;
        ImGui::SetNextWindowPos(ImVec2(main_pos.x + 10, main_pos.y + main_size.y - control_panel_height - 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(main_size.x - 20, control_panel_height), ImGuiCond_Always);

        ImGui::Begin("Control Panel");
        ImGui::Text("Active Devices: %zu", manager.GetAllDevices().size());
        ImGui::Separator();
        
        // [<-] ボタン、スライダー、[->] ボタンを横並びに配置
        if (ImGui::Button("<< Prev")) {
            if (!timestamps.empty()) {
                auto it = std::lower_bound(timestamps.begin(), timestamps.end(), target_time);
                size_t idx = std::distance(timestamps.begin(), it);
                if (idx > 0) {
                    target_time = timestamps[idx - 1];
                } else {
                    target_time = timestamps.front();
                }
            }
        }
        
        ImGui::SameLine();
        
        // Time (s) のスライダー
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 70.0f); // ボタン用のスペースを確保
        ImGui::SliderScalar("##TimeSlider", ImGuiDataType_Double, &target_time, &min_timestamp, &max_timestamp, "Time: %.6f s");
        
        ImGui::SameLine();
        if (ImGui::Button("Next >>")) {
            if (!timestamps.empty()) {
                auto it = std::upper_bound(timestamps.begin(), timestamps.end(), target_time);
                if (it != timestamps.end()) {
                    target_time = *it;
                } else {
                    target_time = timestamps.back();
                }
            }
        }
        ImGui::End();

        // リスト表示用ウィンドウ配置（縦長・横並びレイアウトに変更）
        float available_height = main_size.y - control_panel_height - 30.0f;
        float available_width = main_size.x - 20.0f;
        
        // JSONで設定ファイルが読み込まれており、managerに登録されているアドレスのみをフィルタリング
        std::vector<uint8_t> valid_address_list;
        for (auto& addr : address_list) {
            if (manager.GetDevice(addr) != nullptr) {
                valid_address_list.push_back(addr);
            }
        }

        size_t device_count = valid_address_list.size();
        float device_width = device_count > 0 ? (available_width / static_cast<float>(device_count)) : available_width;

        int i = 0;
        for (auto& addr : valid_address_list) {

            const auto device_ptr = manager.GetDevice(addr);
            const auto state = device_ptr->GetSnapshotViewAt(target_time);

            float current_x = main_pos.x + 10.0f + (i * device_width);
            float current_y = main_pos.y + 10.0f;
            std::string window_name = std::format("{} [0x{:02X}]", device_ptr->GetDeviceName(), addr);

            if (first_layout)
            {
                ImGui::SetNextWindowPos(
                    ImVec2(current_x, current_y));

                ImGui::SetNextWindowSize(
                    ImVec2(
                        device_width - 10.0f,
                        available_height));
            }

            ImGui::Begin(window_name.c_str());

            // --- ★ ここから差し替え ---
            bool is_regless = (device_ptr->GetRegisterAddressBytes() == 0);
            int col_count = is_regless ? 2 : 4;
            std::string table_id = std::format("RegisterListTable_{:X}", addr);

            if (ImGui::BeginTable(table_id.c_str(), col_count, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable)) {
                if (is_regless) {
                    ImGui::TableSetupColumn("Write Data", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableSetupColumn("Read Data", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                } else {
                    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableSetupColumn("Write Data", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableSetupColumn("Read Data", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                }
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                for (const auto& [reg_addr, reg_info] : state.registers) {
                    // WriteデータもReadデータも存在しない場合は行をスキップする
                    if (reg_info.data_w.empty() && reg_info.data_r.empty()) {
                        continue;
                    }

                    ImGui::TableNextRow();

                    // ★ 変更フラグの判定と背景色ハイライト（レジスタの有無にかかわらず適用）
                    bool is_changed = (state.changed_reg_addr == reg_addr);
                    if (is_changed) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(100, 50, 50, 150));
                    }

                    if (is_regless) {
                        // --- レジスタレス（アドレス幅0）の場合 ---
                        ImGui::TableSetColumnIndex(0);
                        for (size_t b_idx = 0; b_idx < reg_info.data_w.size(); ++b_idx) {
                            if (b_idx > 0) ImGui::SameLine(0.0f, 4.0f);
                            ImGui::Text("%02X", reg_info.data_w[b_idx]);
                        }

                        ImGui::TableSetColumnIndex(1);
                        for (size_t b_idx = 0; b_idx < reg_info.data_r.size(); ++b_idx) {
                            if (b_idx > 0) ImGui::SameLine(0.0f, 4.0f);
                            ImGui::Text("%02X", reg_info.data_r[b_idx]);
                        }
                    } else {
                        // --- 従来のレジスタありの場合 ---
                        // ハイライト判定（最後に変更されたレジスタ）
                        bool is_changed = (state.changed_reg_addr == reg_addr);
                        if (is_changed) {
                            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(100, 50, 50, 150));
                        }

                        // 1列目: アドレス
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("0x%04X", reg_addr);

                        // 2列目: レジスタ名
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%s", device_ptr->GetRegisterName(reg_addr).c_str());

                        // ビットフィールドが指定されたバイト範囲に含まれているか判定するヘルパー
                        auto is_byte_in_field = [](size_t b_idx, const auto& bf) {
                            uint32_t start_byte = bf.byte_offset;
                            uint32_t end_byte = bf.byte_offset + (bf.bit_offset + bf.bit_width - 1) / 8;
                            return b_idx >= start_byte && b_idx <= end_byte;
                        };
                        
                        // エンディアン（Little / Big）に対応したビットフィールド値抽出ヘルパー
                        auto extract_bitfield_value = [](const std::vector<uint8_t>& data, const auto& bf) -> uint64_t {
                            if (!bf.is_little_endian) {
                                throw std::runtime_error("Big endian bitfield parsing is not supported.");
                            }

                            uint64_t val = 0;
                            uint32_t effective_width = std::min(static_cast<uint32_t>(bf.bit_width), 64U);

                            for (uint32_t j = 0; j < effective_width; ++j) {
                                uint32_t global_bit = bf.bit_offset + j;
                                uint32_t byte_from_lsb = global_bit / 8;
                                uint32_t bit_in_byte = global_bit % 8;
                                uint32_t physical_byte = bf.byte_offset + byte_from_lsb;

                                if (physical_byte < data.size()) {
                                    uint8_t b = data[physical_byte];
                                    uint8_t bit_val = (b >> bit_in_byte) & 1;
                                    val |= (static_cast<uint64_t>(bit_val) << j);
                                }
                            }
                            return val;
                        };

                        // 3列目: Writeデータ
                        ImGui::TableSetColumnIndex(2);
                        const auto& bit_fields_w = device_ptr->GetWriteRegisterBitField(reg_addr);

                        for (size_t b_idx = 0; b_idx < reg_info.data_w.size(); ++b_idx) {
                            if (b_idx > 0) {
                                ImGui::SameLine(0.0f, 4.0f);
                            }

                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "%02X", reg_info.data_w[b_idx]);
                            ImGui::Text("%s", buf);

                            if (ImGui::IsItemHovered()) {
                                bool has_bits = std::any_of(bit_fields_w.begin(), bit_fields_w.end(), [b_idx, &is_byte_in_field](const auto& bf) {
                                    return is_byte_in_field(b_idx, bf);
                                });

                                if (has_bits) {
                                    ImGui::BeginTooltip();
                                    ImGui::Text("Write Byte %zu Breakdown (0x%04X):", b_idx, reg_addr);
                                    ImGui::Separator();

                                    for (const auto& bf : bit_fields_w) {
                                        if (is_byte_in_field(b_idx, bf)) {
                                            if (bf.bit_width <= 64) {
                                                uint64_t extracted = extract_bitfield_value(reg_info.data_w, bf);
                                                ImGui::Text("  [%d:%d] %s: 0x%llX (%llu)", 
                                                    bf.bit_offset + bf.bit_width - 1, 
                                                    bf.bit_offset, 
                                                    bf.name.c_str(), 
                                                    extracted, 
                                                    extracted);
                                            } else {
                                                ImGui::Text("  [%d:%d] %s: (Width > 64 bits)", 
                                                    bf.bit_offset + bf.bit_width - 1, 
                                                    bf.bit_offset, 
                                                    bf.name.c_str());
                                            }
                                        }
                                    }
                                    ImGui::EndTooltip();
                                }
                            }
                        }

                        // 4列目: Readデータ
                        ImGui::TableSetColumnIndex(3);
                        const auto& bit_fields_r = device_ptr->GetReadRegisterBitField(reg_addr);

                        for (size_t b_idx = 0; b_idx < reg_info.data_r.size(); ++b_idx) {
                            if (b_idx > 0) {
                                ImGui::SameLine(0.0f, 4.0f);
                            }

                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "%02X", reg_info.data_r[b_idx]);
                            ImGui::Text("%s", buf);

                            if (ImGui::IsItemHovered()) {
                                bool has_bits = std::any_of(bit_fields_r.begin(), bit_fields_r.end(), [b_idx, &is_byte_in_field](const auto& bf) {
                                    return is_byte_in_field(b_idx, bf);
                                });

                                if (has_bits) {
                                    ImGui::BeginTooltip();
                                    ImGui::Text("Read Byte %zu Breakdown (0x%04X):", b_idx, reg_addr);
                                    ImGui::Separator();

                                    for (const auto& bf : bit_fields_r) {
                                        if (is_byte_in_field(b_idx, bf)) {
                                            if (bf.bit_width <= 64) {
                                                uint64_t extracted = extract_bitfield_value(reg_info.data_r, bf);
                                                ImGui::Text("  [%d:%d] %s: 0x%llX (%llu)", 
                                                    bf.bit_offset + bf.bit_width - 1, 
                                                    bf.bit_offset, 
                                                    bf.name.c_str(), 
                                                    extracted, 
                                                    extracted);
                                            } else {
                                                ImGui::Text("  [%d:%d] %s: (Width > 64 bits)", 
                                                    bf.bit_offset + bf.bit_width - 1, 
                                                    bf.bit_offset, 
                                                    bf.name.c_str());
                                            }
                                        }
                                    }
                                    ImGui::EndTooltip();
                                }
                            }
                        }
                    }
                }
                ImGui::EndTable();
            }
            ImGui::End();
            i++;
        }
        first_layout = false;

        // レンダリング
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // 5. 後処理
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}