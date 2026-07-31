#include "pch.hpp"
#include "window.hpp"

#include <iostream>


int main()
{
    try
    {
        Window win(600, 600);
        if (!win.Run())
            std::cout << "[INFO]Window was closed" << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[ERROR] " << error.what() << std::endl;
        return 1;
    }
}
