import com.android.build.gradle.LibraryExtension
import com.android.build.gradle.api.LibraryVariant
import com.android.build.gradle.tasks.BundleAar
import org.gradle.api.publish.PublishingExtension

plugins {
    `maven-publish`
    signing
}

val extractorProject = project(":androidx-media-lib-extractor")
val androidExtension = extractorProject.extensions.findByType(LibraryExtension::class.java)
    ?: error("Could not find android extension")

@Suppress("deprecation")
val releaseVariant: LibraryVariant = androidExtension.libraryVariants.first { variant ->
    variant.name == "release"
}

val bundleReleaseAar by extractorProject.tasks.getting(BundleAar::class)

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

val sourcesJar by tasks.registering(Jar::class) {
    archiveClassifier.set("sources")
    val main = androidExtension.sourceSets.getByName("main")
    from(main.java.srcDirs)
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
    publishing.publications.create<MavenPublication>("default") {
        artifactId = "media3-extractor"
        artifact(bundleReleaseAar)
        artifact(javadocJar)
        artifact(sourcesJar)

        pom {
            name.set("mediax Media3 extractor (DV TS + Enhanced FLV)")
            description.set(
                "Patched androidx Media3 extractor: Dolby Vision in HLS/TS (PR #3280) and Enhanced FLV HEVC",
            )
            url.set("https://github.com/vesaaa/mediax")
        }
    }

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
