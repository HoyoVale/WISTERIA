#include "pch.hpp"
#include "window.hpp"

#include <iostream>


int main(){
    Window* win = new Window(600,600);
    if(!win->Run())
        std::cout << "[INFO]Window was closed" << std::endl;
    return 0;
}