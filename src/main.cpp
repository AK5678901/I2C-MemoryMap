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
#include "imgui_memory_editor.h"

#include "i2c_device.hpp"
#include "json.hpp"

// nlohmann/json のエイリアス
using json = nlohmann::json;

namespace {
    constexpr size_t MAX_DEVICE = 3;
    I2CDeviceManager manager;
    double max_timestamp = 0.0; // データの最大時刻を保持
    double min_timestamp = std::numeric_limits<double>::max(); // データの最小時刻を保持
    std::vector<double> timestamps; // 全てのユニークなタイムスタンプ（1ステップずつの移動用）
    std::vector<uint8_t> address_list; // ログに出てきたデバイスだけ画面に出す用。I2Cログ上に出てきたI2Cアドレスを格納しておく
}

// Windowsのファイル選択ダイアログを開いてパスを取得する関数
std::wstring OpenFileDialog() {
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL; // 必要に応じてGLFWのウィンドウハンドルを指定可能
    ofn.lpstrFilter = L"CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return std::wstring(ofn.lpstrFile);
    }
    return L"";
}

// 同階層の devices.json からデバイス設定を読み込んで登録する関数
bool loadDeviceConfig(I2CDeviceManager& manager) {
    std::filesystem::path p = "devices.json";
    if (!std::filesystem::exists(p)) {
        std::cerr << "Config file not found: " << p.string() << std::endl;
        return false;
    }

    std::ifstream file(p);
    if (!file.is_open()) return false;

    try {
        json j;
        file >> j;

        if (j.contains("devices") && j["devices"].is_array()) {
            for (const auto& dev : j["devices"]) {
                std::string name = dev.value("name", "UNKNOWN");
                std::string addr_str = dev.value("address", "0x00");
                int addr_width = dev.value("addr_width", 1);

                // 16進数文字列を uint8_t に変換
                uint8_t addr = static_cast<uint8_t>(std::stoul(addr_str, nullptr, 16));

                manager.RegisterDevice(name, addr, addr_width);
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "JSON Parse Error: " << e.what() << std::endl;
        return false;
    }
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
            bool exists = std::ranges::contains(address_list, addr_7bit);
            if (!exists) {
                address_list.push_back(addr_7bit);
            }

            bool is_read = (read_str == "true");
            crnt_device = manager.GetDevice(addr_7bit);    
            if (crnt_device != nullptr){
                crnt_device->CallThisEachI2CAddrByteWrite(is_read);
            }
        } else if (type == "\"data\"") {
            if (crnt_device != nullptr) {
                uint8_t data = static_cast<uint8_t>(std::stoul(data_str, nullptr, 16));
                crnt_device->DataByte(data, timestamp);
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

int main() {
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
        MessageBoxW(NULL, L"Failed to load 'devices.json' from the executable directory.", L"Error", MB_OK | MB_ICONERROR);
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

    // MemoryEditorインスタンス
    static MemoryEditor mem_edit;
    static std::map<uint8_t, MemoryEditor> editors;

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
        // デバイス用ウィンドウの配置計算
        // 1デバイスにつき「Write用」「Read用」の2つのウィンドウを並べるため、高さを計算
        // デバイス用ウィンドウの配置計算
        // 1デバイスにつき「Write用」「Read用」の2つのウィンドウを並べるため、高さを計算
        float available_height = main_size.y - control_panel_height - 30.0f;
        int device_count = std::min(address_list.size(), MAX_DEVICE);
        float device_height = available_height / static_cast<float>(device_count);

        int i = 0;
        for (auto& addr : address_list) {
            const auto device_ptr = manager.GetDevice(addr);

            // SnapshotView を取得（std::vector<bool> や memory が含まれている）
            const auto state = device_ptr->GetSnapshotViewAt(target_time);
            size_t changed_index = state.changed_index;

            float half_width = main_size.x / 2.0f;
            float current_y = main_pos.y + 10 + (i * device_height);

            // 共通のヘッダー文字列
            std::string hex_header = "      +0 +1 +2 +3 +4 +5 +6 +7 +8 +9 +A +B +C +D +E +F";

            // ----------------------------------------------------
            // 1. Write用ウィンドウ
            // ----------------------------------------------------
            std::string window_name_w = std::format("{} - Write [0x{:02X}]", device_ptr->GetDeviceName(), addr);
            ImGui::SetNextWindowPos(ImVec2(main_pos.x, current_y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(half_width, device_height - 10), ImGuiCond_Always);

            ImGui::Begin(window_name_w.c_str());
            ImGui::TextUnformatted(hex_header.c_str());
            ImGui::Separator();
            
            static std::map<uint8_t, MemoryEditor> editors_w;
            auto& ed_w = editors_w[addr];
            ed_w.OptGreyOutZeroes = false;
            ed_w.OptAddrDigitsCount = 4;

            // ★ ハイライト（最後に変更されたインデックスを光らせる）
            ed_w.HighlightFn = [](const ImU8* mem, size_t off, void* user_data) {
                auto* st = reinterpret_cast<const I2CDevice::SnapshotView*>(user_data);
                if (st->is_write && st->changed_index >= 0 && off == static_cast<size_t>(st->changed_index)){
                    return true;
                }
                return false;
            };

            // ★ 背景色コールバック: 一度もWriteされていないバイトはダークグレーにする
            ed_w.BgColorFn = [](const ImU8* mem, size_t off, void* user_data) -> ImU32 {
                auto* st = reinterpret_cast<const I2CDevice::SnapshotView*>(user_data);
                if (!st) return 0;
                if (off < st->byte_has_written.size() && !st->byte_has_written[off]) {
                    return IM_COL32(70, 70, 70, 180); // 未アクセス：グレー
                }
                return 0; // アクセス済み：デフォルト
            };

            ed_w.UserData = const_cast<void*>(reinterpret_cast<const void*>(&state));
            ed_w.HighlightColor = IM_COL32(255, 120, 120, 150); // Write変更ハイライト（赤系）
            ed_w.DrawContents(const_cast<uint8_t*>(state.memory_w.data()), state.memory_w.size(), 0);
            ImGui::End();

            // ----------------------------------------------------
            // 2. Read用ウィンドウ
            // ----------------------------------------------------
            std::string window_name_r = std::format("{} - Read [0x{:02X}]", device_ptr->GetDeviceName(), addr);
            ImGui::SetNextWindowPos(ImVec2(main_pos.x + half_width, current_y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(half_width, device_height - 10), ImGuiCond_Always);

            ImGui::Begin(window_name_r.c_str());
            ImGui::TextUnformatted(hex_header.c_str());
            ImGui::Separator();
            
            static std::map<uint8_t, MemoryEditor> editors_r;
            auto& ed_r = editors_r[addr];
            ed_r.OptGreyOutZeroes = false;
            ed_r.OptAddrDigitsCount = 4;

            // ★ ハイライト（最後に変更されたインデックスを光らせる）
            ed_r.HighlightFn = [](const ImU8* mem, size_t off, void* user_data) {
                auto* st = reinterpret_cast<const I2CDevice::SnapshotView*>(user_data);
                if (!st->is_write && st->changed_index >= 0 && off == static_cast<size_t>(st->changed_index)){
                    return true;
                }
                return false;
            };

            // ★ 背景色コールバック: 一度もReadされていないバイトはダークグレーにする
            ed_r.BgColorFn = [](const ImU8* mem, size_t off, void* user_data) -> ImU32 {
                auto* st = reinterpret_cast<const I2CDevice::SnapshotView*>(user_data);
                if (!st) return 0;
                if (off < st->byte_has_read.size() && !st->byte_has_read[off]) {
                    return IM_COL32(70, 70, 70, 180); // 未アクセス：グレー
                }
                return 0; // アクセス済み：デフォルト
            };

            ed_r.UserData = const_cast<void*>(reinterpret_cast<const void*>(&state));
            ed_r.HighlightColor = IM_COL32(255, 120, 120, 150); // 赤系
            ed_r.DrawContents(const_cast<uint8_t*>(state.memory_r.data()), state.memory_r.size(), 0);
            ImGui::End();

            i++;
        }

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