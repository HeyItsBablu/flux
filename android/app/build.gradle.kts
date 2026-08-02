import groovy.json.JsonSlurper
import java.awt.Image
import java.awt.image.BufferedImage
import javax.imageio.ImageIO

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
val fluxIconPath = config["icon"]     as? String

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


// ============================================================================
// App icon — regenerate mipmap PNGs from config/<icon> on every build
// ============================================================================
val fluxIconDensities = mapOf(
    "mipmap-mdpi"    to 48,
    "mipmap-hdpi"    to 72,
    "mipmap-xhdpi"   to 96,
    "mipmap-xxhdpi"  to 144,
    "mipmap-xxxhdpi" to 192
)

val generateIcons by tasks.registering {
    val resDir = file("src/main/res")
    val iconSource = fluxIconPath?.let { rootProject.file("../$it") }

    if (iconSource != null) {
        inputs.file(iconSource)
    }
    outputs.dirs(fluxIconDensities.keys.map { File(resDir, it) })

    notCompatibleWithConfigurationCache("Uses java.awt/ImageIO APIs that cannot be serialized into the configuration cache")

    doLast {
        if (iconSource == null || !iconSource.exists()) {
            logger.warn("Flux: icon source not found (${iconSource}) — skipping icon generation")
            return@doLast
        }
        val src = ImageIO.read(iconSource)

        fluxIconDensities.forEach { (dir, size) ->
            val outDir = File(resDir, dir)
            outDir.mkdirs()


            // Remove stale .webp variants so they don't collide with the
            // .png we're about to (re)generate — same resource name,
            // different extension = "duplicate resource" at merge time.
            listOf("ic_launcher.webp", "ic_launcher_round.webp").forEach {
                File(outDir, it).delete()
            }
            
            fun resize(target: Int): BufferedImage {
                val scaled = src.getScaledInstance(target, target, Image.SCALE_SMOOTH)
                val out = BufferedImage(target, target, BufferedImage.TYPE_INT_ARGB)
                val g = out.createGraphics()
                g.drawImage(scaled, 0, 0, null)
                g.dispose()
                return out
            }

            ImageIO.write(resize(size), "png", File(outDir, "ic_launcher.png"))
            ImageIO.write(resize(size), "png", File(outDir, "ic_launcher_round.png"))
        }
    }
}

tasks.named("preBuild") {
    dependsOn(generateIcons)
}