// Installed WISTERIA C++ SDK smoke test.
//
// Usage:
//   wisteria_cpp_sdk_consumer [model_path] [render]
//
// Without a model it only exercises Context/version/ABI. With a model it
// additionally creates an Entity, prepares frame zero, captures/restores a
// checkpoint and serializes it. Passing "render" also runs a 64x64 offline
// render through the C++ wrapper.
#include "wisteria/sdk/wisteria_sdk.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[])
{
    try
    {
        wisteria::sdk::Context context;

        const std::uint32_t abi = context.RuntimeAbiVersion();
        if (abi != WISTERIA_STABLE_RUNTIME_ABI_VERSION)
        {
            std::cerr << "unexpected runtime ABI: " << abi << '\n';
            return 1;
        }

        if (argc > 1)
        {
            wisteria::sdk::Entity entity(context, argv[1]);
            const WisteriaRuntimeCapabilitiesV1 capabilities =
                entity.Capabilities();
            (void)capabilities;

            entity.PrepareFrameZero();
            entity.StepExact(1U);

            wisteria::sdk::Checkpoint checkpoint(context, entity);
            const WisteriaCheckpointInfoV1 info = checkpoint.Info();
            (void)info;

            const std::vector<std::uint8_t> bytes =
                checkpoint.Serialize();
            if (bytes.empty())
                throw std::runtime_error("checkpoint serialized to zero bytes");

            checkpoint.Restore(entity);

            if (argc > 2 && std::string(argv[2]) == "render")
            {
                wisteria::sdk::RenderSession session(context);
                const std::vector<std::uint8_t> pixels =
                    session.RenderOffline(
                        entity,
                        wisteria::sdk::RenderCamera{},
                        64U,
                        64U
                    );
                if (pixels.empty())
                    throw std::runtime_error("offline render returned no pixels");
            }
        }

        std::cout << "WISTERIA C++ SDK consumer OK (product="
                  << wisteria::sdk::Context::ProductVersion()
                  << ", runtime ABI v" << abi << ")\n";
        return 0;
    }
    catch (const wisteria::sdk::StatusError& error)
    {
        std::cerr << "[STATUS " << error.Code() << "] "
                  << error.what() << '\n';
        return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[ERROR] " << error.what() << '\n';
        return 3;
    }
}
