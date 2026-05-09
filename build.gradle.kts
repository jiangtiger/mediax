import com.android.build.gradle.LibraryExtension

plugins {
    id("io.github.gradle-nexus.publish-plugin") version "2.0.0"
}

buildscript {
    repositories {
        mavenCentral()
        google()
    }
    dependencies {
        classpath("com.android.tools.build:gradle:8.12.0")
        classpath("org.jetbrains.kotlin:kotlin-gradle-plugin:2.2.0")
    }
}

allprojects {
    group = "org.jellyfin.media3"
    version = createVersion()

    repositories {
        mavenCentral()
        google()
    }

    // Keep consumer compatibility with AGP 8.6 / compileSdk 35 projects.
    // media submodule release branch may default to compileSdk 36, which raises
    // AAR minCompileSdk and breaks downstream apps pinned to 35.
    afterEvaluate {
        val android = extensions.findByType(LibraryExtension::class.java)
        if (android != null) {
            android.compileSdk = 35
            android.defaultConfig.minSdk = 21
            android.ndkVersion = "26.1.10909125"
        }
    }
}

// Add Sonatype publishing repository
nexusPublishing {
    repositories.sonatype {
        nexusUrl.set(uri("https://ossrh-staging-api.central.sonatype.com/service/local/"))
        snapshotRepositoryUrl.set(uri("https://central.sonatype.com/repository/maven-snapshots/"))

        username.set(getProperty("ossrh.username"))
        password.set(getProperty("ossrh.password"))
    }

    useStaging.set(project.provider { project.version.toString() != SNAPSHOT_VERSION })
}

tasks.wrapper {
    distributionType = Wrapper.DistributionType.ALL
}

tasks.register<Delete>("clean") {
    delete(rootProject.layout.buildDirectory)
}
