#include <exception>
#include <iostream>

void run_font_tests();
void run_grid_tests();
void run_ui_events_tests();
void run_input_tests();
void run_unicode_tests();

int main()
{
    try
    {
        run_font_tests();
        run_grid_tests();
        run_ui_events_tests();
        run_input_tests();
        run_unicode_tests();
        std::cout << "spectre-tests: ok\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "spectre-tests: " << ex.what() << '\n';
        return 1;
    }
}
