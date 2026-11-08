plugins { id("com.android.application"); id("org.jetbrains.kotlin.android") }
android {
    namespace = "com.loader.app"
    compileSdk = 34
    defaultConfig {
        applicationId = "com.loader.app"
        minSdk = 24
        targetSdk = 28
        versionCode = 1
        versionName = "0.1"
        ndk { abiFilters += "armeabi-v7a" }
        externalNativeBuild { cmake { arguments += "-DANDROID_STL=c++_shared" } }
    }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt") } }
    buildFeatures { prefab = true }
    packaging { jniLibs { pickFirsts += "**/libshadowhook.so"; pickFirsts += "**/libc++_shared.so" } }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
}
dependencies {
    compileOnly(project(":stub"))   // MainActivity palsu, tidak ikut masuk APK
    implementation("com.bytedance.android:shadowhook:1.0.9")
    implementation("org.lsposed.hiddenapibypass:hiddenapibypass:4.3")
}
