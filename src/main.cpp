#include "flux.hpp"
#include <windows.h>
#include <string>

// Timer Component
class TimerComponent : public StatefulComponent
{
private:
    State<int> seconds;
    State<bool> isRunning;
    State<std::string> displayTime;

    // Format seconds as MM:SS
    std::string formatTime(int totalSeconds)
    {
        int mins = totalSeconds / 60;
        int secs = totalSeconds % 60;
        
        char buffer[10];
        sprintf(buffer, "%02d:%02d", mins, secs);
        return std::string(buffer);
    }

public:
    TimerComponent(FluxUI *ctx) 
        : StatefulComponent(ctx),
          seconds(0, ctx),
          isRunning(false, ctx),
          displayTime("00:00", ctx)
    {
    }

    void initState() override
    {
        // Update display whenever seconds change
        seconds.listen([this](int s) {
            displayTime.set(formatTime(s));
        });
        
        // Note: In a real app, you'd need a timer callback
        // For this example, we'll just have manual increment buttons
    }

    WidgetPtr build() override
    {
        return Scaffold(
           
            
            Center(
                Card(
                    Column(
                        // Timer Display
                        Text(displayTime)
                            ->setFontSize(72)
                            ->setFontWeight(FontWeight::Bold)
                            ->setTextColor(RGB(33, 150, 243)),
                        
                        SizedBox(0, 40),
                        
                        // Status
                        Text(isRunning.get() ? "Running..." : "Stopped")
                            ->setFontSize(18)
                            ->setTextColor(isRunning.get() ? RGB(76, 175, 80) : RGB(158, 158, 158)),
                        
                        SizedBox(0, 40),
                        
                        // Control Buttons
                        Row(
                            Button(isRunning.get() ? "Pause" : "Start", [this]() {
                                isRunning.toggle();
                            })
                            ->setBackgroundColor(RGB(76, 175, 80))
                            ->setMinWidth(100),
                            
                            SizedBox(20, 0),
                            
                            Button("Reset", [this]() {
                                seconds.set(0);
                                isRunning.set(false);
                            })
                            ->setBackgroundColor(RGB(244, 67, 54))
                            ->setMinWidth(100)
                        ),
                        
                        SizedBox(0, 30),
                        
                        Divider(),
                        
                        SizedBox(0, 30),
                        
                        // Manual Time Controls (simulating timer ticks)
                        Text("Manual Controls")
                            ->setFontSize(14)
                            ->setTextColor(RGB(158, 158, 158)),
                        
                        SizedBox(0, 10),
                        
                        Row(
                            Button("+1 sec", [this]() {
                                seconds++;
                            })
                            ->setBackgroundColor(RGB(33, 150, 243)),
                            
                            SizedBox(10, 0),
                            
                            Button("+10 sec", [this]() {
                                seconds += 10;
                            })
                            ->setBackgroundColor(RGB(33, 150, 243)),
                            
                            SizedBox(10, 0),
                            
                            Button("+1 min", [this]() {
                                seconds += 60;
                            })
                            ->setBackgroundColor(RGB(33, 150, 243))
                        )
                    )
                    ->setSpacing(0)
                )
                ->setWidth(400)
                ->setPadding(30)
            )
        );
    }

    void dispose() override
    {
        seconds.clearListeners();
        isRunning.clearListeners();
        displayTime.clearListeners();
        StatefulComponent::dispose();
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    FluxUI app(hInstance);

    // ✅ CRITICAL: Keep component alive!
    auto timerComponent = ComponentBuilder::create<TimerComponent>(&app);

    app.build([timerComponent]() {
        return timerComponent->build();
    });

    app.createWindow("Timer Example - Component Based", 500, 500);
    return app.run();
}