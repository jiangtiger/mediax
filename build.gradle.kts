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

    // Keep Android library modules compatible with downstream AGP 8.6/compileSdk 35 apps.
    plugins.withId("com.android.library") {
        extensions.findByType(LibraryExtension::class.java)?.apply {
            compileSdk = 36
            defaultConfig.minSdk = 21
            ndkVersion = "26.1.10909125"
            // common_config.gradle in upstream may set aarMetadata.minCompileSdk=compileSdk (36).
            // Override again after project evaluation so published AARs stay consumable by compileSdk 35 apps.
            project.afterEvaluate {
                defaultConfig.aarMetadata.minCompileSdk = 35
            }
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
