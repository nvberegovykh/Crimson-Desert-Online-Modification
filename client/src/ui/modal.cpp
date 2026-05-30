// SPDX-License-Identifier: MIT

#include "modal.h"
#include "../session.h"
#include "../util/log.h"

#include <windows.h>
#include <cctype>
#include <cstring>

#include <imgui.h>

namespace cdmp {

namespace {
void CopyToClipboard(const std::string& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (mem) {
        char* dst = static_cast<char*>(GlobalLock(mem));
        if (dst) {
            memcpy(dst, text.c_str(), text.size() + 1);
            GlobalUnlock(mem);
            SetClipboardData(CF_TEXT, mem);
        }
    }
    CloseClipboard();
}

const char* StatusLabel(SessionStatus s) {
    switch (s) {
        case SessionStatus::Disconnected: return "Disconnected";
        case SessionStatus::Connecting: return "Connecting...";
        case SessionStatus::Connected: return "Connected";
        case SessionStatus::InSession: return "In session";
        case SessionStatus::Failed: return "Failed";
    }
    return "";
}
} // namespace

Modal& Modal::Get() {
    static Modal instance;
    return instance;
}

void Modal::Open() {
    if (state_ == ModalState::CLOSED) {
        state_ = Session::Get().Status() == SessionStatus::InSession
                     ? ModalState::IN_SESSION
                     : ModalState::MAIN_MENU;
    }
}

void Modal::Close() { state_ = ModalState::CLOSED; }

void Modal::Toggle() {
    if (IsOpen()) {
        Close();
    } else {
        Open();
    }
}

void Modal::ApplyTheme() {
    if (themeApplied_) return;
    themeApplied_ = true;
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 6.f;
    s.FrameRounding = 4.f;
    s.GrabRounding = 4.f;
    s.WindowBorderSize = 1.f;
    s.WindowPadding = ImVec2(16, 16);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(10, 10);

    // Slight blue tint accent (#1a3a5c).
    const ImVec4 accent = ImVec4(0.102f, 0.227f, 0.361f, 1.0f);
    const ImVec4 accentHover = ImVec4(0.16f, 0.32f, 0.5f, 1.0f);
    const ImVec4 accentActive = ImVec4(0.22f, 0.42f, 0.62f, 1.0f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.07f, 0.09f, 0.94f);
    c[ImGuiCol_TitleBgActive] = accent;
    c[ImGuiCol_Button] = accent;
    c[ImGuiCol_ButtonHovered] = accentHover;
    c[ImGuiCol_ButtonActive] = accentActive;
    c[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.14f, 0.18f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = accentHover;
    c[ImGuiCol_Header] = accent;
    c[ImGuiCol_HeaderHovered] = accentHover;
    c[ImGuiCol_CheckMark] = ImVec4(0.4f, 0.7f, 1.0f, 1.0f);
    c[ImGuiCol_Border] = ImVec4(0.16f, 0.32f, 0.5f, 0.6f);
}

void Modal::Render() {
    if (state_ == ModalState::CLOSED) return;
    ApplyTheme();

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Always);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("Crimson Desert Multiplayer", nullptr, flags)) {
        switch (state_) {
            case ModalState::MAIN_MENU: RenderMainMenu(); break;
            case ModalState::HOST_SETUP: RenderHostSetup(); break;
            case ModalState::JOIN_SETUP: RenderJoinSetup(); break;
            case ModalState::IN_SESSION: RenderInSession(); break;
            default: break;
        }
    }
    ImGui::End();
}

void Modal::RenderMainMenu() {
    ImGui::PushFont(nullptr);
    ImGui::TextColored(ImVec4(0.5f, 0.75f, 1.0f, 1.0f), "Multiplayer");
    ImGui::PopFont();
    ImGui::Separator();
    ImGui::Spacing();

    const bool inSession = Session::Get().Status() == SessionStatus::InSession;
    const ImVec2 btn(ImGui::GetContentRegionAvail().x, 36);

    if (ImGui::Button("Host Session", btn)) state_ = ModalState::HOST_SETUP;
    if (ImGui::Button("Join Session", btn)) state_ = ModalState::JOIN_SETUP;

    if (!inSession) ImGui::BeginDisabled();
    if (ImGui::Button("Leave Session", btn)) {
        Session::Get().LeaveSession();
    }
    if (!inSession) ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::TextDisabled("Status: %s", StatusLabel(Session::Get().Status()));
}

void Modal::RenderHostSetup() {
    ImGui::TextColored(ImVec4(0.5f, 0.75f, 1.0f, 1.0f), "Host a Session");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::InputText("Server", serverHost_, sizeof(serverHost_));
    ImGui::InputInt("Port", &serverPort_);
    ImGui::InputText("Session name (optional)", sessionName_, sizeof(sessionName_));
    ImGui::InputText("Password (optional)", password_, sizeof(password_),
                     ImGuiInputTextFlags_Password);

    ImGui::Spacing();
    const ImVec2 btn(ImGui::GetContentRegionAvail().x * 0.5f - 5, 32);
    if (ImGui::Button("Start Hosting", btn)) {
        Session::Get().HostSession(serverHost_,
                                   static_cast<uint16_t>(serverPort_),
                                   sessionName_, password_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Back", btn)) state_ = ModalState::MAIN_MENU;

    const SessionStatus st = Session::Get().Status();
    if (st == SessionStatus::Connecting) {
        ImGui::TextDisabled("Connecting...");
    } else if (st == SessionStatus::Failed) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s",
                           Session::Get().LastError().c_str());
    } else if (st == SessionStatus::InSession && Session::Get().IsHost()) {
        const std::string code = Session::Get().InviteCode();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Your invite code:");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.85f, 1.0f, 1.0f));
        // Big code text via larger scale.
        ImGui::SetWindowFontScale(2.2f);
        ImGui::Text("%s", code.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        if (ImGui::Button("Copy to clipboard")) CopyToClipboard(code);
        ImGui::SameLine();
        if (ImGui::Button("Enter Session")) state_ = ModalState::IN_SESSION;
    }
}

void Modal::RenderJoinSetup() {
    ImGui::TextColored(ImVec4(0.5f, 0.75f, 1.0f, 1.0f), "Join a Session");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::InputText("Server", serverHost_, sizeof(serverHost_));
    ImGui::InputInt("Port", &serverPort_);
    if (ImGui::InputText("Invite code", inviteCode_, sizeof(inviteCode_),
                         ImGuiInputTextFlags_CharsUppercase)) {
        // Force uppercase defensively (flag handles most input methods).
        for (char& ch : inviteCode_) ch = static_cast<char>(std::toupper(ch));
    }
    ImGui::InputText("Password (optional)", password_, sizeof(password_),
                     ImGuiInputTextFlags_Password);

    ImGui::Spacing();
    const ImVec2 btn(ImGui::GetContentRegionAvail().x * 0.5f - 5, 32);
    if (ImGui::Button("Connect", btn)) {
        Session::Get().JoinSession(serverHost_,
                                   static_cast<uint16_t>(serverPort_),
                                   inviteCode_, password_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Back", btn)) state_ = ModalState::MAIN_MENU;

    const SessionStatus st = Session::Get().Status();
    if (st == SessionStatus::Connecting) {
        ImGui::TextDisabled("Connecting...");
    } else if (st == SessionStatus::Failed) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s",
                           Session::Get().LastError().c_str());
    } else if (st == SessionStatus::InSession) {
        state_ = ModalState::IN_SESSION;
    }
}

void Modal::RenderInSession() {
    Session& sess = Session::Get();
    ImGui::TextColored(ImVec4(0.5f, 0.75f, 1.0f, 1.0f), "Session");
    ImGui::Separator();

    if (sess.IsHost()) {
        ImGui::Text("You are the host  (code: %s)", sess.InviteCode().c_str());
    } else {
        ImGui::Text("Connected to %s", sess.ServerName().c_str());
    }
    ImGui::TextDisabled("Friendly fire: %s",
                        sess.FriendlyFire() ? "ON" : "OFF");
    ImGui::Spacing();

    if (ImGui::BeginTable("players", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
        ImGui::TableSetupColumn("Player");
        ImGui::TableSetupColumn("Health");
        ImGui::TableSetupColumn("Ping");
        ImGui::TableSetupColumn("Host");
        ImGui::TableHeadersRow();

        for (const RemotePlayer& p : sess.Players().GetRemotePlayers()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            std::string label = p.name.empty() ? p.id : p.name;
            if (p.inCutscene) label += " (cutscene)";
            ImGui::TextUnformatted(label.c_str());
            ImGui::TableSetColumnIndex(1);
            const float frac =
                p.maxHealth > 0 ? p.currentHealth / p.maxHealth : 0.f;
            ImGui::ProgressBar(frac, ImVec2(-1, 14));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d ms", p.ping);
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(p.isHost ? "*" : "");
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::Button("Leave Session", ImVec2(-1, 32))) {
        sess.LeaveSession();
        state_ = ModalState::MAIN_MENU;
    }
}

} // namespace cdmp
