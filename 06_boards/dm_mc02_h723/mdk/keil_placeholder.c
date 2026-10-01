/* Keil placeholder: lets the Keil project link so that "After Build" can replace the output
   with the CMake (GCC) ELF. The real firmware is built by CMake (build_with_cmake.bat, ADR 0052).
   Keil's generated scatter file needs a section named RESET (normally the vector table). */
__attribute__((section("RESET"), used)) const int keil_placeholder = 0;
