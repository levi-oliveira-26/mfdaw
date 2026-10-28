#include <cstdio>
#include <juce_core/juce_core.h>

int main()
{
    juce::UnitTestRunner runner;
    runner.runAllTests();

    int failures = 0;
    for (int suite = 0; suite < runner.getNumResults(); ++suite)
    {
        const auto* result = runner.getResult (suite);
        if (result != nullptr)
        {
            failures += result->failures;
            std::printf ("%s - %d passed, %d failed\n",
                         result->unitTestName.toRawUTF8(),
                         result->passes,
                         result->failures);
        }
    }

    std::printf ("%d total failures\n", failures);
    return failures == 0 ? 0 : 1;
}
