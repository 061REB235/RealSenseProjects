// Generated with ImRAD 0.7
// visit https://github.com/tpecholt/imrad

#include "gui.h"

Gui gui;


void Gui::Draw(GLFWwindow* window)
{
    /// @style Dark
    /// @unit px
    /// @begin TopWindow
    auto* ioUserData = (ImRad::IOUserData*)ImGui::GetIO().UserData;
    glfwSetWindowTitle(window, "myTitle");
    ImGui::SetNextWindowPos({ 0, 0 });
    int tmpWidth, tmpHeight;
    glfwGetWindowSize(window, &tmpWidth, &tmpHeight);
    ImGui::SetNextWindowSize({ (float)tmpWidth, (float)tmpHeight });
    bool tmpOpen;
    if (ImGui::Begin("###Gui", &tmpOpen, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
    {
        if (ImGui::IsWindowAppearing())
        {
            glfwSetWindowSize(window, 1935, 1400);
            glfwSetWindowAttrib(window, GLFW_RESIZABLE, false);
            glfwSetWindowAttrib(window, GLFW_DECORATED, true);
        }
        /// @separator

        // TODO: Add Draw calls of dependent popup windows here

        /// @begin Child
        ImGui::BeginChild("child1", { 1920, 1080 }, ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoSavedSettings);
        {
            /// @separator

            /// @separator
            ImGui::EndChild();
        }
        /// @end Child

        /// @begin Child
        ImGui::BeginChild("child2", { 300, 275 }, ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoSavedSettings);
        {
            /// @separator

            /// @begin Slider
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("##value1", &value1, 0, 1, nullptr);
            /// @end Slider

            /// @begin Slider
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("##value2", &value2, 0, 1, nullptr);
            /// @end Slider

            /// @begin Slider
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("##value3", &value3, 0, 1, nullptr);
            /// @end Slider

            /// @separator
            ImGui::EndChild();
        }
        /// @end Child

        /// @separator
        ImGui::End();
    }
    /// @end TopWindow
}
