#include "App/AppController.h"
#include "Core/AppPaths.h"
#include "Plugins/PluginScanner.h"
#include "UI/KmLookAndFeel.h"
#include "UI/MainComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace
{
void writeCrashLog (void*)
{
    km::paths::crashLogFile().appendText ("\n==== Crash " + juce::Time::getCurrentTime().toString (true, true) + " ====\n"
                                          + juce::SystemStats::getStackBacktrace());
}

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, juce::Component* content)
        : DocumentWindow (name, km::colours::background, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, true);
        setResizeLimits (900, 500, 10000, 10000);

        const auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        if (display != nullptr && display->userBounds.getWidth() < getWidth())
            setFullScreen (true);
        else
            centreWithSize (getWidth(), getHeight());

        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};
} // namespace

//==============================================================================
class KlankModuleApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "KlankModule"; }
    const juce::String getApplicationVersion() override { return KM_VERSION_STRING; }

    // The plug-in scanner runs as extra copies of this program, so those must be allowed.
    bool moreThanOneInstanceAllowed() override { return km::isPluginScanCommandLine (getCommandLineParameters()); }

    void initialise (const juce::String& commandLine) override
    {
        if (km::isPluginScanCommandLine (commandLine))
        {
            setApplicationReturnValue (km::runPluginScanChild (getCommandLineParameterArray()));
            quit();
            return;
        }

        logger =std::make_unique<juce::FileLogger> (km::paths::logFile(), "KlankModule " + getApplicationVersion(), 512 * 1024);
        juce::Logger::setCurrentLogger (logger.get());
        juce::SystemStats::setApplicationCrashHandler (writeCrashLog);

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        controller = std::make_unique<km::AppController>();
        controller->initialise();

        juce::Desktop::getInstance().setGlobalScaleFactor ((float) controller->getSettings().getDoubleValue ("uiScale", 1.0));
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), new km::MainComponent (*controller));
        openFromCommandLine (commandLine);
    }

    /** Double-clicked .kmsong / .kmperf file (file association from the installer). */
    void openFromCommandLine (const juce::String& commandLine)
    {
        auto path = commandLine.trim().unquoted().trim();
        if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
            return;
        const juce::File file (path);
        if (! file.existsAsFile() || controller == nullptr)
            return;
        if (file.hasFileExtension (".kmsong"))
        {
            if (controller->getSongController().loadSong (file))
                if (auto* main = dynamic_cast<km::MainComponent*> (mainWindow != nullptr ? mainWindow->getContentComponent() : nullptr))
                    main->showArrangerView (true);
        }
        else if (file.hasFileExtension (".kmperf"))
        {
            controller->loadPerformance (file);
        }
    }

    void shutdown() override
    {
        mainWindow.reset();        // closes plug-in windows first
        if (controller != nullptr)
            controller->shutdown(); // stops audio, saves everything
        controller.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        juce::Logger::setCurrentLogger (nullptr);
        logger.reset();
    }

    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        openFromCommandLine (commandLine);
        if (mainWindow != nullptr)
        {
            mainWindow->setMinimised (false);
            mainWindow->toFront (true);
        }
    }

private:
    km::KmLookAndFeel lookAndFeel;
    std::unique_ptr<juce::FileLogger> logger;
    std::unique_ptr<km::AppController> controller;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (KlankModuleApplication)
