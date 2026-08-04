{
  "targets": [
    {
      "target_name": "wisteria_native_demo",
      "sources": [ "binding.cc" ],
      "cflags": [ "-std=c++17", "-fexceptions" ],
      "cflags_cc": [ "-std=c++17", "-fexceptions" ],
      "xcode_settings": {
        "CLANG_CXX_LANGUAGE_STANDARD": "c++17"
      },
      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 1,
          "AdditionalOptions": [ "/std:c++17" ]
        }
      }
    }
  ]
}
