plugins { id("com.android.library") }
android {
    namespace = "com.mojang.stub"
    compileSdk = 34
    defaultConfig { minSdk = 24 }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
}
