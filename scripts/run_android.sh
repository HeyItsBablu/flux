#!/bin/bash
cd "$(dirname "$0")/../android"
./gradlew installDebug
adb shell am start -n com.example.myapplication/.MainActivity