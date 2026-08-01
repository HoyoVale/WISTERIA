#include "pch.hpp"
#include "application.hpp"
#include "window.hpp"

#include <iostream>


int main()
{
    try
    {
        Application application;
        application.CreateWindow(WindowConfig{
            .width = 600,
            .height = 600,
            .title = "FLORAL WISTERIA"
        });
        const int result = application.Run();
        std::cout << "[INFO] Application was closed" << std::endl;
        return result;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[ERROR] " << error.what() << std::endl;
        return 1;
    }
}
