#!/usr/bin/env groovy
 
import hudson.EnvVars
import groovy.json.JsonSlurperClassic
import groovy.json.JsonBuilder
import groovy.json.JsonOutput
import java.net.URL


def CloneRepo(String relative_target_dir,
              String gerrit_url,
              String gerrit_group,
              String gerrit_project) {

    stage('Clone repo') {
	    checkout([$class: 'GitSCM',
		branches: [[name: '$GERRIT_BRANCH']],
		doGenerateSubmoduleConfigurations: false,
		extensions: [
		    [$class: 'RelativeTargetDirectory', relativeTargetDir: "${relative_target_dir}"],
		    [$class: 'BuildChooserSetting', buildChooser: [$class: 'GerritTriggerBuildChooser']],
                    [$class: 'CleanBeforeCheckout']
		],
		submoduleCfg: [],
		userRemoteConfigs: [
		    [name: 'origin',
		     refspec: '$GERRIT_REFSPEC',
		     url: "${gerrit_url}/${gerrit_group}/${gerrit_project}"]
		    ]
		]
	    )

    }
}

try {
    node {
        stage '\u2776 Checkout simulator'
        //echo "\u2600 BUILD_URL=${env.BUILD_URL}"
        def workspace = pwd()

        echo "\u2600 workspace=${workspace}"
        CloneRepo("./simulator", "https://review.gerrithub.io", "davidsaOpenu", "simulator")

        // evaluate implicitly creates a class based on the filename specified
//        evaluate(new File("${workspace}/simulator/infra/jjb/Clone.groovy"))

        // Safer to use 'def' here as Groovy seems fussy about whether
        // the filename (and therefore implicit class name) has a capital first letter
//        def tu = new Clone()
//        tu.myUtilityMethod("hello world")

        File sourceFile = new File("${workspace}/simulator/infra/jjb/Clone.groovy");
        Class groovyClass = new GroovyClassLoader(getClass().getClassLoader()).parseClass(sourceFile);
        GroovyObject myObject = (GroovyObject) groovyClass.newInstance();
        myObject.myUtilityMethod("hello world")

    } // node
} // try end

catch (e) {
    currentBuild.result = "FAILURE"

    echo 'Err: Incremental Build failed with Error: ' + e.toString()
    echo '     Trying to build with a clean Workspace'
    throw e

} finally {
  
    (currentBuild.result != "ABORTED") && node("master") {
        // Send e-mail notifications for failed or unstable builds.
        // currentBuild.result must be non-null for this step to work.
    }
}
