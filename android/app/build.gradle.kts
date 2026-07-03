import groovy.json.JsonSlurper

plugins {
    alias(libs.plugins.android.application)
}

// ============================================================================
// App Configuration — read from repo root config/AppConfig.json
// ============================================================================
val configFile = rootProject.file("../config/AppConfig.json")
val config = JsonSlurper().parse(configFile) as Map<*, *>
val fluxAppName  = config["name"]     as String
val fluxBundleId = config["bundleId"] as String
val fluxVersion  = config["version"]  as String
val fluxBuildNum = config["build"]    as Int

android {
    namespace  = fluxBundleId
    compileSdk = 36


    defaultConfig {
        applicationId = fluxBundleId
        minSdk        = 28
        targetSdk     = 36
        versionCode   = fluxBuildNum
        versionName   = fluxVersion
        resValue("string", "app_name", fluxAppName)

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    sourceSets {
        getByName("main") {
            assets.srcDirs(
                "src/main/assets",
                "../../assets"   
            )
        }
    }

    buildTypes {
        release {
            optimization {
                enable = false
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    buildFeatures {
        viewBinding = true
        resValues   = true  
    }
}

dependencies {
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.constraintlayout)
    implementation(libs.androidx.core.ktx)
    implementation(libs.material)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(libs.androidx.junit)
}