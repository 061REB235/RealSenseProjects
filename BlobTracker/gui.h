// Generated with ImRAD 0.7
// visit https://github.com/tpecholt/imrad

#pragma once
#define IMRAD_WITH_GLFW
#include <imrad.h>

class Gui
{
public:
    /// @begin interface
    void Draw(GLFWwindow* window);

    float value1;
    float value2;
    float value3;
    /// @end interface

private:
    /// @begin impl

    /// @end impl
};

extern Gui gui;
