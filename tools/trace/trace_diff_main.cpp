#include "trace_jsonl.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr
            << "usage: wisteria_trace_diff <trace-a.jsonl> <trace-b.jsonl>\n";
        return 2;
    }
    const std::filesystem::path pathA(argv[1]);
    const std::filesystem::path pathB(argv[2]);
    std::ifstream inputA(pathA);
    std::ifstream inputB(pathB);
    if (!inputA.is_open() || !inputB.is_open())
    {
        std::cerr << "cannot open trace files\n";
        return 2;
    }
    wisteria::trace::TraceDiffResult result =
        wisteria::trace::DiffTraceStreams(inputA, inputB);
    result.profileA = pathA.filename().string();
    result.profileB = pathB.filename().string();
    std::cout << wisteria::trace::FormatTraceDiff(result);
    return result.identical ? 0 : 1;
}
