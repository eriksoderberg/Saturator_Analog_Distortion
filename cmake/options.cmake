cmake_minimum_required(VERSION 3.24)

# ----------------------------------------------------------------------
# pongasoft dependencies (re-cmake, re-logging, re-mock)
#
# As of Feb 2026 the pongasoft projects moved from GitHub to Codeberg.
# The legacy GitHub release archive URLs are intermittently / no longer
# served (re-logging's GitHub archive in particular has stopped
# responding). Rather than chase URL+hash churn, we point FetchContent
# at local clones that sit alongside the project.
#
# Expected on-disk layout:
#   /Users/eriksoderberg/Rack_Extension_Development/
#       re-cmake/        (v1.8.5 or newer master from Codeberg)
#       re-logging/      (v2.0.2 from Codeberg)
#       re-mock/         (v1.8.1 from Codeberg)
#       Projects/
#           Saturator/   (this project)
#
# Clone the missing ones with:
#   cd /Users/eriksoderberg/Rack_Extension_Development
#   git clone --branch v2.0.2 https://codeberg.org/pongasoft/re-logging.git
#   git clone --branch v1.8.1 https://codeberg.org/pongasoft/re-mock.git
#
# To revert to remote fetching, blank out the *_ROOT_DIR values and
# re-enable the GIT_REPO / DOWNLOAD_URL blocks below — preserved as
# comments for that purpose.
# ----------------------------------------------------------------------

#################
# re-cmake
#################
set(re-cmake_GIT_TAG "v1.8.5" CACHE STRING "re-cmake git tag")
set(re-cmake_GIT_REPO "https://codeberg.org/pongasoft/re-cmake" CACHE STRING "re-cmake git repository url")
set(re-cmake_DOWNLOAD_URL "" CACHE STRING "re-cmake download url" FORCE)
set(re-cmake_DOWNLOAD_URL_HASH "" CACHE STRING "re-cmake download url hash" FORCE)

set(re-cmake_ROOT_DIR "${RE_PROJECT_ROOT_DIR}/../../re-cmake")

#################
# re-logging
#################
set(re-logging_GIT_REPO "https://codeberg.org/pongasoft/re-logging" CACHE STRING "re-logging git repository url")
set(re-logging_GIT_TAG "v2.0.2" CACHE STRING "re-logging git tag")
set(re-logging_DOWNLOAD_URL "" CACHE STRING "re-logging download url" FORCE)
set(re-logging_DOWNLOAD_URL_HASH "" CACHE STRING "re-logging download url hash" FORCE)

set(re-logging_ROOT_DIR "${RE_PROJECT_ROOT_DIR}/../../re-logging")

#################
# re-mock
#################
set(re-mock_GIT_REPO "https://codeberg.org/pongasoft/re-mock" CACHE STRING "re-mock git repository url")
set(re-mock_GIT_TAG "v1.8.1" CACHE STRING "re-mock git tag")
set(re-mock_DOWNLOAD_URL "" CACHE STRING "re-mock download url" FORCE)
set(re-mock_DOWNLOAD_URL_HASH "" CACHE STRING "re-mock download url hash" FORCE)

set(re-mock_ROOT_DIR "${RE_PROJECT_ROOT_DIR}/../../re-mock")

#################
# googletest
#################
set(googletest_GIT_REPO "https://github.com/google/googletest" CACHE STRING "googletest git repository URL")
set(googletest_GIT_TAG "v1.17.0" CACHE STRING "googletest git tag")
set(googletest_DOWNLOAD_URL "${googletest_GIT_REPO}/archive/refs/tags/${googletest_GIT_TAG}.zip" CACHE STRING "googletest download url" FORCE)
set(googletest_DOWNLOAD_URL_HASH "SHA256=40d4ec942217dcc84a9ebe2a68584ada7d4a33a8ee958755763278ea1c5e18ff" CACHE STRING "googletest download url hash" FORCE)

# Uncomment/set to proper value if local instead of remote
set(googletest_ROOT_DIR "") # use remote googletest
#set(re-googletest_ROOT_DIR "${RE_PROJECT_ROOT_DIR}/../../google/googletest")

