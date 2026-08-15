pipeline {
    agent none

    options {
        buildDiscarder(logRotator(numToKeepStr: '10'))
        timestamps()
        timeout(time: 30, unit: 'MINUTES')
    }

    parameters {
        choice(
            name: 'BUILD_TYPE',
            choices: ['Release', 'Debug'],
            description: 'CMake build type'
        )
        booleanParam(
            name: 'RUN_COVERAGE',
            defaultValue: false,
            description: 'Run code coverage analysis'
        )
        booleanParam(
            name: 'RUN_STATIC_ANALYSIS',
            defaultValue: false,
            description: 'Run clang-tidy static analysis'
        )
        booleanParam(
            name: 'RUN_SANITIZERS',
            defaultValue: false,
            description: 'Run Address Sanitizer tests'
        )
    }

    stages {
        stage('Build & Test Matrix') {
            matrix {
                axes {
                    axis {
                        name 'PLATFORM'
                        values 'linux-gcc', 'linux-clang'
                    }
                }
                stages {
                    stage('Build') {
                        agent {
                            docker {
                                image 'ubuntu:22.04'
                                args '-u root'
                            }
                        }
                        environment {
                            CC = "${PLATFORM == 'linux-gcc' ? 'gcc' : 'clang'}"
                            CXX = "${PLATFORM == 'linux-gcc' ? 'g++' : 'clang++'}"
                        }
                        steps {
                            sh '''
                                apt-get update
                                apt-get install -y build-essential cmake ninja-build clang
                            '''
                            sh '''
                                cmake -B build \
                                    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
                                    -DWireLab_ENABLE_UNIT_TESTING=ON \
                                    -DWireLab_ENABLE_CCACHE=OFF \
                                    -G Ninja
                            '''
                            sh 'cmake --build build --config ${BUILD_TYPE}'
                        }
                    }
                    stage('Test') {
                        agent {
                            docker {
                                image 'ubuntu:22.04'
                                args '-u root'
                                reuseNode true
                            }
                        }
                        steps {
                            sh 'cd build && ctest -C ${BUILD_TYPE} --output-on-failure --verbose'
                        }
                        post {
                            always {
                                junit allowEmptyResults: true, testResults: 'build/**/test-results.xml'
                            }
                        }
                    }
                }
            }
        }

        stage('Code Quality') {
            parallel {
                stage('Formatting Check') {
                    agent {
                        docker {
                            image 'ubuntu:22.04'
                            args '-u root'
                        }
                    }
                    steps {
                        sh '''
                            apt-get update
                            apt-get install -y clang-format
                        '''
                        sh '''
                            find include/ src/ test/ -name '*.hpp' -o -name '*.cpp' | \
                            xargs clang-format --dry-run --Werror
                        '''
                    }
                }

                stage('Static Analysis') {
                    when {
                        expression { params.RUN_STATIC_ANALYSIS }
                    }
                    agent {
                        docker {
                            image 'ubuntu:22.04'
                            args '-u root'
                        }
                    }
                    environment {
                        CC = 'clang'
                        CXX = 'clang++'
                    }
                    steps {
                        sh '''
                            apt-get update
                            apt-get install -y build-essential cmake ninja-build clang clang-tidy
                        '''
                        sh '''
                            cmake -B build \
                                -DCMAKE_BUILD_TYPE=Debug \
                                -DWireLab_ENABLE_UNIT_TESTING=ON \
                                -DWireLab_ENABLE_CLANG_TIDY=ON \
                                -DWireLab_ENABLE_CCACHE=OFF \
                                -G Ninja
                        '''
                        sh 'cmake --build build --config Debug'
                    }
                }

                stage('Code Coverage') {
                    when {
                        expression { params.RUN_COVERAGE }
                    }
                    agent {
                        docker {
                            image 'ubuntu:22.04'
                            args '-u root'
                        }
                    }
                    environment {
                        CC = 'gcc'
                        CXX = 'g++'
                    }
                    steps {
                        sh '''
                            apt-get update
                            apt-get install -y build-essential cmake ninja-build lcov
                        '''
                        sh '''
                            cmake -B build \
                                -DCMAKE_BUILD_TYPE=Debug \
                                -DWireLab_ENABLE_UNIT_TESTING=ON \
                                -DWireLab_ENABLE_CODE_COVERAGE=ON \
                                -DWireLab_ENABLE_CCACHE=OFF \
                                -G Ninja
                        '''
                        sh 'cmake --build build --config Debug'
                        sh 'cd build && ctest -C Debug --output-on-failure --verbose'
                        sh '''
                            cd build
                            lcov --capture --directory . --output-file coverage.info
                            lcov --remove coverage.info '/usr/*' '*/test/*' '*/build/_deps/*' --output-file coverage.info
                            lcov --list coverage.info
                        '''
                    }
                    post {
                        success {
                            publishHTML(target: [
                                allowMissing: true,
                                alwaysLinkToLastBuild: true,
                                keepAll: true,
                                reportDir: 'build',
                                reportFiles: 'coverage.info',
                                reportName: 'Code Coverage'
                            ])
                        }
                    }
                }

                stage('Address Sanitizer') {
                    when {
                        expression { params.RUN_SANITIZERS }
                    }
                    agent {
                        docker {
                            image 'ubuntu:22.04'
                            args '-u root'
                        }
                    }
                    environment {
                        CC = 'clang'
                        CXX = 'clang++'
                    }
                    steps {
                        sh '''
                            apt-get update
                            apt-get install -y build-essential cmake ninja-build clang
                        '''
                        sh '''
                            cmake -B build \
                                -DCMAKE_BUILD_TYPE=Debug \
                                -DWireLab_ENABLE_UNIT_TESTING=ON \
                                -DWireLab_ENABLE_ASAN=ON \
                                -DWireLab_ENABLE_CCACHE=OFF \
                                -G Ninja
                        '''
                        sh 'cmake --build build --config Debug'
                        sh 'cd build && ctest -C Debug --output-on-failure --verbose'
                    }
                }
            }
        }

        stage('Docker E2E Test') {
            when {
                branch 'main'
            }
            agent {
                label 'docker'
            }
            steps {
                sh 'docker build -t vpn-alpine -f tests/Dockerfile .'
                sh 'chmod +x tests/test_in_docker.sh && ./tests/test_in_docker.sh'
            }
        }
    }

    post {
        always {
            node('built-in') {
                cleanWs()
            }
        }
        failure {
            echo 'Pipeline failed!'
        }
        success {
            echo 'Pipeline succeeded!'
        }
    }
}
