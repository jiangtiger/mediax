import com.android.build.gradle.LibraryExtension
import com.android.build.gradle.api.LibraryVariant
import com.android.build.gradle.tasks.BundleAar
import org.gradle.api.publish.PublishingExtension
import org.gradle.api.tasks.Sync
import org.gradle.api.tasks.bundling.Zip
import java.util.Properties

plugins {
    `maven-publish`
    signing
}

val decoderProject = project(":androidx-media-lib-decoder-ffmpeg")
val androidExtension = decoderProject.extensions.findByType(LibraryExtension::class.java)
    ?: error("Could not find android extension")

@Suppress("deprecation") // libraryVariants exposes deprecated type
val releaseVariant: LibraryVariant = androidExtension.libraryVariants.first { variant ->
    variant.name == "release"
}

val bundleReleaseAar by decoderProject.tasks.getting(BundleAar::class)

val compatAarUnpackDir = layout.buildDirectory.dir("compat/aar-unpacked")
val compatAarPatchedDir = layout.buildDirectory.dir("compat/aar-patched")

val unpackReleaseAarForCompat by tasks.registering(Sync::class) {
    dependsOn(bundleReleaseAar)
    from(zipTree(bundleReleaseAar.archiveFile))
    into(compatAarUnpackDir)
}

val patchReleaseAarMetadataForCompat by tasks.registering {
    dependsOn(unpackReleaseAarForCompat)
    doLast {
        copy {
            from(compatAarUnpackDir)
            into(compatAarPatchedDir)
        }
        val metadataFile = compatAarPatchedDir.get().file("META-INF/com/android/build/gradle/aar-metadata.properties").asFile
        val props = Properties()
        metadataFile.inputStream().use { props.load(it) }
        props.setProperty("minCompileSdk", "35")
        metadataFile.outputStream().use { props.store(it, null) }
    }
}

val compatBundleReleaseAar by tasks.registering(Zip::class) {
    dependsOn(patchReleaseAarMetadataForCompat)
    archiveBaseName.set("media3-ffmpeg-decoder")
    archiveClassifier.set("")
    archiveVersion.set(project.version.toString())
    archiveExtension.set("aar")
    destinationDirectory.set(layout.buildDirectory.dir("compat"))
    from(compatAarPatchedDir)
}

val generateJavadoc by tasks.registering(Javadoc::class) {
    group = "publishing"
    description = "Generates Javadoc for release sources."

    val javaCompile = releaseVariant.javaCompileProvider.get()
    source = javaCompile.source
    classpath = javaCompile.classpath + files(androidExtension.bootClasspath)

    setDestinationDir(File(buildDir, "/docs/javadoc"))

    isFailOnError = false
}

val javadocJar by tasks.registering(Jar::class) {
    archiveClassifier.set("javadoc")

    from(generateJavadoc)
    dependsOn(generateJavadoc)
}

// Package sources from decoder project
val sourcesJar by tasks.registering(Jar::class) {
    archiveClassifier.set("sources")

    val main = androidExtension.sourceSets.getByName("main")
    from(main.java.srcDirs)
    @Suppress("deprecation")
    from(main.jni.srcDirs) {
        exclude("**/ffmpeg/")
    }
}

configure<PublishingExtension> {
    val token = System.getenv("GITHUB_TOKEN").orEmpty()
    val slug = System.getenv("GITHUB_REPOSITORY").orEmpty()
    if (token.isNotEmpty() && slug.isNotEmpty()) {
        repositories.maven {
            name = "GitHubPackages"
            url = uri("https://maven.pkg.github.com/$slug")
            credentials {
                username = System.getenv("GITHUB_ACTOR").orEmpty()
                password = token
            }
        }
    }
}

afterEvaluate {
    // Specify release artifacts and apply POM
    publishing.publications.create<MavenPublication>("default") {
        // Repackage release artifact and patch AAR metadata for downstream compileSdk 35 compatibility.
        artifact(compatBundleReleaseAar) {
            extension = "aar"
            classifier = null
        }

        // Add JavaDocs and sources
        artifact(javadocJar)
        artifact(sourcesJar)

        pom {
            name.set("Jellyfin AndroidX Media3 libraries - $artifactId")
            description.set("AndroidX Media3 FFmpeg decoder used in the Jellyfin project")
            url.set("https://github.com/vesaaa/mediax")

            scm {
                connection.set("scm:git:git://github.com/vesaaa/mediax.git")
                developerConnection.set("scm:git:ssh://git@github.com/vesaaa/mediax.git")
                url.set("https://github.com/vesaaa/mediax/tree/main")
            }

            licenses {
                license {
                    name.set("GNU General Public License v3.0")
                    url.set("https://www.gnu.org/licenses/gpl-3.0.txt")
                }
            }

            developers {
                developer {
                    id.set("Maxr1998")
                    name.set("Max Rumpf")
                    url.set("https://github.com/Maxr1998")
                    organization.set("Jellyfin")
                    organizationUrl.set("https://jellyfin.org")
                }

                developer {
                    id.set("nielsvanvelzen")
                    name.set("Niels van Velzen")
                    url.set("https://github.com/nielsvanvelzen")
                    organization.set("Jellyfin")
                    organizationUrl.set("https://jellyfin.org")
                }
            }

        }
    }

    // Add signing config
    configure<SigningExtension> {
        val signingKey = getProperty("signing.key")
        val signingPassword = getProperty("signing.password") ?: ""

        if (signingKey != null) {
            useInMemoryPgpKeys(signingKey, signingPassword)
            val publishing: PublishingExtension by project
            sign(publishing.publications)
        }
    }
}
