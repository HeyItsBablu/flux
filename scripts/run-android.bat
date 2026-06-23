@echo off
cd %~dp0\..\android
call gradlew.bat installDebug
adb shell am start -n com.example.myapplication/.MainActivity