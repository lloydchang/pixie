/**
 * Jenkins build definition. This file defines the entire build pipeline.
 */
import java.net.URLEncoder;
import groovy.json.JsonBuilder

/**
  * We expect the following parameters to be defined (for code review builds):
  *    PHID: Which should be the buildTargetPHID from Harbormaster.
  *    INITIATOR_PHID: Which is the PHID of the initiator (ie. Differential)
  *    API_TOKEN: The api token to use to communicate with Phabricator
  *    REVISION: The revision ID of the Differential.
  */

final String PHAB_URL = 'https://phab.pixielabs.ai'
final String PHAB_API_URL = "${PHAB_URL}/api"

final String DEV_DOCKER_IMAGE = 'pl-dev-infra/dev_image'
final String SRC_STASH_NAME = "${BUILD_TAG}_src"
/**
  * @brief Generates URL for harbormaster.
  */
def harborMasterUrl = {
  method ->
    url = "${PHAB_API_URL}/${method}?api.token=${params.API_TOKEN}" +
            "&buildTargetPHID=${params.PHID}"
    return url
}

/**
 * @brief Sends build status to Phabricator.
 */
def sendBuildStatus = {
  build_status ->
    def url = harborMasterUrl("harbormaster.sendmessage") + "&type=${build_status}"
    httpRequest consoleLogResponseBody: true,
      contentType: 'APPLICATION_JSON',
      httpMode: 'POST',
      requestBody:'',
      responseHandle: 'NONE',
      url: url,
      validResponseCodes: '200'
}

/**
  * @brief Add build info to harbormaster and badge to Jenkins.
  */
def addBuildInfo = {
  def encodedDisplayUrl = URLEncoder.encode(env.RUN_DISPLAY_URL, 'UTF-8')
  def url = harborMasterUrl("harbormaster.createartifact")
  url += "&buildTargetPHID=${params.PHID}"
  url += '&artifactKey=jenkins.uri'
  url += '&artifactType=uri'
  url += "&artifactData[uri]=${encodedDisplayUrl}"
  url += '&artifactData[name]=Jenkins'
  url += '&artifactData[ui.external]=true'

  httpRequest consoleLogResponseBody: true,
    contentType: 'APPLICATION_JSON',
    httpMode: 'POST',
    requestBody: '',
    responseHandle: 'NONE',
    url: url,
    validResponseCodes: '200'

  def text = ""
  def link = ""
  // Either a revision of a commit to master.
  if (params.REVISION) {
    def revisionId = "D${REVISION}"
    text = revisionId
    link = "${PHAB_URL}/${revisionId}"
  } else {
    text = params.PHAB_COMMIT.substring(0, 7)
    link = "${PHAB_URL}/rPLM${env.PHAB_COMMIT}"
  }
  addShortText(text: text,
    background: "transparent",
    border: 0,
    borderColor: "transparent",
    color: "#1FBAD6",
    link: link)

}

/**
 * @brief Returns true if it's a phabricator triggered build.
 *  This could either be code review build or master commit.
 */
def isPhabricatorTriggeredBuild() {
  return params.PHID != null && params.PHID != ""
}

def codeReviewPreBuild = {
  sendBuildStatus('work')
  addBuildInfo()
}

def codeReviewPostBuild = {
  if (currentBuild.result == "SUCCESS") {
    sendBuildStatus('pass')
  } else {
    sendBuildStatus('fail')
  }
}

def writeBazelRCFile() {
  def bazelRcFile = [
    'common --color=yes',
    // Build arguments.
    'build --announce_rc',
    'build --verbose_failures',
    'build --jobs=16',
    // Build remote jobs setup.
    'build --remote_http_cache=http://bazel-cache.internal.pixielabs.ai:9090',
    'build --remote_local_fallback=true',
    'build --remote_local_fallback_strategy=local',
    'build --remote_timeout=10',
    'build --experimental_remote_retry',
    // Test remote jobs setup.
    'test --remote_timeout=10',
    'test --remote_local_fallback=true',
    'test --remote_local_fallback_strategy=local',
    // Other test args.
    'test --verbose_failures',
  ].join('\n')
  writeFile file: "jenkins.bazelrc", text: "${bazelRcFile}"
}

String devDockerImageWithTag = '';
def builders = [:]
builders['Build & Test (dbg)'] = {
  node {
    sh 'rm -rf /root/.cache/bazel'
    deleteDir()
    unstash SRC_STASH_NAME
    docker.withRegistry('https://gcr.io', 'gcr:pl-dev-infra') {
      docker.image(devDockerImageWithTag).inside('-v /root/.cache:/root/.cache') {
        sh 'scripts/bazel_fetch_retry.sh'
        sh 'bazel test -c dbg //...'
        stash name: 'build-dbg-testlogs', includes: "bazel-testlogs/**"
      }
    }
  }
}

builders['Build & Test (opt)'] = {
  node {
    sh 'rm -rf /root/.cache/bazel'
    deleteDir()
    unstash SRC_STASH_NAME
    docker.withRegistry('https://gcr.io', 'gcr:pl-dev-infra') {
      docker.image(devDockerImageWithTag).inside('-v /root/.cache:/root/.cache') {
        sh 'scripts/bazel_fetch_retry.sh'
        sh 'bazel test -c opt //...'
        stash name: 'build-opt-testlogs', includes: "bazel-testlogs/**"
      }
    }
  }
}


builders['Build & Test (gcc:opt)'] = {
  node {
    sh 'rm -rf /root/.cache/bazel'
    deleteDir()
    unstash SRC_STASH_NAME
    docker.withRegistry('https://gcr.io', 'gcr:pl-dev-infra') {
      docker.image(devDockerImageWithTag).inside('-v /root/.cache:/root/.cache') {
        sh 'scripts/bazel_fetch_retry.sh'
        sh 'CC=gcc CXX=g++ bazel test -c opt //...'
        stash name: 'build-gcc-opt-testlogs', includes: "bazel-testlogs/**"
      }
    }
  }
}

// Only run coverage on master test.
if (env.JOB_NAME == "pixielabs-master") {
  builders['Build & Test (gcc:coverage)'] = {
    node {
      sh 'rm -rf /root/.cache/bazel'
      deleteDir()
      unstash SRC_STASH_NAME
      docker.withRegistry('https://gcr.io', 'gcr:pl-dev-infra') {
        docker.image(devDockerImageWithTag).inside('-v /root/.cache:/root/.cache') {
          sh 'scripts/bazel_fetch_retry.sh'
          sh 'scripts/collect_coverage.sh -u -t ${CODECOV_TOKEN} -b master -c `cat GIT_COMMIT`'
          stash name: 'build-gcc-coverage-testlogs', includes: "bazel-testlogs/**"
        }
      }
    }
  }
}

/********************************************
 * For now restrict the ASAN and TSAN builds to carnot. There is a bug in go(or llvm) preventing linking:
 * https://github.com/golang/go/issues/27110
 * TODO(zasgar): Fix after above is resolved.
 ********************************************/
builders['Build & Test (asan)'] = {
  node {
    sh 'rm -rf /root/.cache/bazel'
    deleteDir()
    unstash SRC_STASH_NAME
    docker.withRegistry('https://gcr.io', 'gcr:pl-dev-infra') {
      docker.image(devDockerImageWithTag).inside('-v /root/.cache:/root/.cache --cap-add=SYS_PTRACE') {
        sh 'scripts/bazel_fetch_retry.sh'
        sh 'bazel test --config=asan //src/carnot/...'
        stash name: 'build-asan-testlogs', includes: "bazel-testlogs/**"
      }
    }
  }
}

builders['Build & Test (tsan)'] = {
  node {
    sh 'rm -rf /root/.cache/bazel'
    deleteDir()
    unstash SRC_STASH_NAME
    docker.withRegistry('https://gcr.io', 'gcr:pl-dev-infra') {
      docker.image(devDockerImageWithTag).inside('-v /root/.cache:/root/.cache --cap-add=SYS_PTRACE') {
        sh 'scripts/bazel_fetch_retry.sh'
        sh 'bazel test --config=tsan //src/carnot/...'
        stash name: 'build-tsan-testlogs', includes: "bazel-testlogs/**"
      }
    }
  }
}

/********************************************
 * The build script starts here.
 ********************************************/
if (isPhabricatorTriggeredBuild()) {
  codeReviewPreBuild()
}

node {
  currentBuild.result = 'SUCCESS'
  deleteDir()
  try {
    stage('Checkout code') {
      checkout scm
      sh '''
        printenv
        # Store the GIT commit in a file, since the git plugin has issues with
        # the Jenkins pipeline system.
        git rev-parse HEAD > GIT_COMMIT
      '''
      writeBazelRCFile()

      // Get docker image tag.
      properties = readProperties file: 'docker.properties'
      devDockerImageWithTag = DEV_DOCKER_IMAGE + ":${properties.DOCKER_IMAGE_TAG}"
      stash name: SRC_STASH_NAME
    }
    stage('Lint') {
      unstash SRC_STASH_NAME
      docker.withRegistry('https://gcr.io', 'gcr:pl-dev-infra') {
        docker.image(devDockerImageWithTag).inside {
          sh 'arc lint --everything'
        }
      }
    }
    stage('Build') {
      parallel(builders)
    }
    stage('Build & Test UI') {
      docker.withRegistry('https://gcr.io', 'gcr:pl-dev-infra') {
        docker.image(devDockerImageWithTag).inside {
          sh '''
            cd src/ui
            yarn install --prefer_offline
            jest
          '''
        }
      }
    }
    stage('Archive') {
      dir ('build-opt-testlogs') {
        unstash 'build-opt-testlogs'
      }
      dir ('build-gcc-opt-testlogs') {
        unstash 'build-gcc-opt-testlogs'
      }
      dir ('build-dbg-testlogs') {
        unstash 'build-dbg-testlogs'
      }
      dir ('build-asan-testlogs') {
        unstash 'build-asan-testlogs'
      }
      dir ('build-tsan-testlogs') {
        unstash 'build-tsan-testlogs'
      }
      if (env.JOB_NAME == "pixielabs-master") {
        dir ('build-gcc-coverage-testlogs') {
          unstash 'build-gcc-coverage-testlogs'
        }
      }
      step([
        $class: 'XUnitBuilder',
        thresholds: [
          [
            $class: 'FailedThreshold',
            unstableThreshold: '1'
          ]
        ],
        tools: [
          [
            $class: 'GoogleTestType',
            pattern: "build*/bazel-testlogs/**/*.xml"
          ]
        ]
      ])

      step([
        $class: 'XUnitBuilder',
        thresholds: [
          [
            $class: 'FailedThreshold',
            unstableThreshold: '1'
          ]
        ],
        tools: [
          [
            $class: 'JUnitType',
            pattern: "src/ui/junit.xml"
          ]
        ]
      ])
    }
  }
  catch(err) {
    currentBuild.result = 'FAILURE'
    echo "Exception thrown:\n ${err}"
    echo "Stacktrace:"
    err.printStackTrace()
  }
  finally {
    if (isPhabricatorTriggeredBuild()) {
      codeReviewPostBuild()
    }
  }
}
