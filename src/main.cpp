#include "pch.hpp"
#include "window.hpp"

#include <iostream>


int main(){
    Window* win = new Window();
    if(!win->Run())
        std::cout << "[INFO]Window was closed" << std::endl;
    return 0;
}