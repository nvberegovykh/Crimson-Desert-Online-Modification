// SPDX-License-Identifier: MIT
//
// ImGui multiplayer modal. Elden Ring style host/join flow.

#pragma once

namespace cdmp {

enum class ModalState {
    CLOSED,
    MAIN_MENU,
    HOST_SETUP,
    JOIN_SETUP,
    IN_SESSION,
};

class Modal {
public:
    static Modal& Get();

    void Open();
    void Close();
    void Toggle();
    bool IsOpen() const { return state_ != ModalState::CLOSED; }

    // Apply the dark/blue theme once.
    void ApplyTheme();

    // Render the modal for the current frame (call between NewFrame/Render).
    void Render();

private:
    Modal() = default;
    void RenderMainMenu();
    void RenderHostSetup();
    void RenderJoinSetup();
    void RenderInSession();

    ModalState state_ = ModalState::CLOSED;
    bool themeApplied_ = false;

    // Input buffers.
    char serverHost_[128] = "127.0.0.1";
    int serverPort_ = 7777;
    char sessionName_[64] = "";
    char password_[64] = "";
    char inviteCode_[8] = "";
};

} // namespace cdmp
