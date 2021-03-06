#!/bin/bash

# Have to be in the same order as the output of `ls *.exe bin/*.exe bin/*.dll bin/*.json`
FILES_LIST="bin/act_nut_lib.dll bin/bmpfont_create_gdi.dll bin/bmpfont_create_gdiplus.dll bin/jansson.dll bin/libpng16.dll bin/steam_api.dll bin/thcrap_configure.exe bin/thcrap.dll bin/thcrap_i18n.dll bin/thcrap_loader.exe bin/thcrap_tasofro.dll bin/thcrap_test.exe bin/thcrap_tsa.dll bin/thcrap_update.dll bin/update.json bin/vc_redist.x86.exe bin/win32_utf8.dll bin/zlib-ng.dll thcrap_configure.exe thcrap_loader.exe"

# Arguments. Every argument without a default value is mandatory.
DATE="$(date)"
MSBUILD_PATH=
MSBUILD_USER="$USER"
GITHUB_LOGIN=
# Authentication with Github tokens: https://developer.github.com/v3/auth/#basic-authentication
GITHUB_TOKEN=

function parse_input
{
    while [ $# -gt 0 ]; do
        case "$1" in
            --help )
                echo "Usage: $0 arguments"
                echo '  --help:          Display this message'
                echo '  --date:          Release date, in the format YYYY-MM-DD (or any other date supported by `date -d`).'
                echo '                   Optional, defaults to today'
                echo '  --msbuild-path:  Path to msbuild.exe'
                echo '  --msbuild-user:  Name of the user building this release (displayed in the thcrap logs).'
                echo '                   Optional, defaults to $USER.'
                echo '  --github-login:  Username for Github'
                echo '  --github-token:  Token for Github'
                echo '                   To get a token, see https://help.github.com/en/github/authenticating-to-github/creating-a-personal-access-token-for-the-command-line'
                exit 0
                ;;
            --date )
                DATE=$2
                shift 2
                ;;
            --msbuild-path )
                MSBUILD_PATH=$2
                shift 2
                ;;
            --msbuild-user )
                MSBUILD_USER=$2
                shift 2
                ;;
            --github-login )
                GITHUB_LOGIN=$2
                shift 2
                ;;
            --github-token )
                GITHUB_TOKEN=$2
                shift 2
                ;;
            * )
                echo "Unknown argument $1"
                exit 1
        esac
    done
    
    if [ -z "$DATE" ];          then echo "--date is required"          ; exit 1; fi
    if [ -z "$MSBUILD_PATH" ];  then echo "--msbuild-path is required"  ; exit 1; fi
    if [ -z "$MSBUILD_USER" ];  then echo "--msbuild-user is required"  ; exit 1; fi
    if [ -z "$GITHUB_LOGIN" ];  then echo "--github-login is required"  ; exit 1; fi
    if [ -z "$GITHUB_TOKEN" ];  then echo "--github-token is required"  ; exit 1; fi
}

function	confirm
{
    read -p "$1 [y/N] " -n 1 -r
    echo    # (optional) move to a new line
    if [[ ! $REPLY =~ ^[Yy]$ ]]
    then
	[[ "$0" = "$BASH_SOURCE" ]] && exit 1 || return 1 # handle exits from shell or function but don't exit interactive shell
    fi
}

# Fill the argument constants at the top of this file
parse_input "$@"

# Pull changes
cd git_thcrap
git pull
git submodule update --init --recursive
git status
confirm "Continue?"

# Update the version number in global.cpp, and commit
if ! grep "return 0x$(date -d "$DATE" +%Y%m%d);" thcrap/src/global.cpp ; then
    sed -e "s/return 0x20[12][0-9][01][0-9][0-3][0-9];/return 0x$(date -d "$DATE" +%Y%m%d);/" -i thcrap/src/global.cpp
    git add thcrap/src/global.cpp
    git commit -m "Update version number"
fi
cd ..

# build
cd git_thcrap
"$MSBUILD_PATH" /target:Rebuild /property:USERNAME=$MSBUILD_USER /property:Configuration=Release /verbosity:minimal | \
	grep -e warning -e error | \
	grep -v -e 'Number of'
cd ..

# Prepare the release directory
if [ "$(cd git_thcrap/bin && echo $(ls *.exe bin/*.exe bin/*.dll bin/*.json | grep -vF '_d.dll'))" != "$FILES_LIST" ]; then
	echo "The list of files to copy doesn't match. Files list:"
	cd git_thcrap/bin
	ls *.exe bin/*.exe bin/*.dll bin/*.json | grep -vF '_d.dll'
	cd -
	confirm "Continue anuway?"
fi
rm -rf thcrap # Using -f for readonly files
mkdir thcrap
mkdir thcrap/bin
# Copy all the build files
for f in $FILES_LIST; do cp git_thcrap/bin/$f thcrap/$f; done
cp -r git_thcrap/scripts/ thcrap/bin/
rm -rf thcrap/bin/scripts/__pycache__/
# Add an initial repo.js, used by configure as a server list
mkdir -p thcrap/repos/thpatch
cd thcrap/repos/thpatch
wget http://srv.thpatch.net/repo.js
cd ../../..

# Copy the release to the test directory
mkdir -p thcrap_test # don't fail if it exists
cp -r thcrap/* thcrap_test/

cd thcrap_test
explorer.exe .
cd ..
confirm "Time to test the release! Press y if everything works fine."

# Create the zip and its signature
rm -f thcrap.zip
cd thcrap
7z a ../thcrap.zip *
cd ..
python3 ./git_thcrap/scripts/release_sign.py -k cert.pem thcrap.zip

rm -f thcrap_symbols.zip
cd git_thcrap/bin
7z a ../../thcrap_symbols.zip *
cd ../..

echo 'Pushing "Update version number" on thcrap'
git -C git_thcrap push

# Create the commits history
commits="$(git -C git_thcrap log --reverse --format=oneline $(git -C git_thcrap tag | tail -1)^{commit}..HEAD~1)"

cat > commit_github.txt <<EOF
Add a release comment if you want to.

##### \`module_name\`
$(echo "$commits" | sed -e 's/^\([0-9a-fA-F]\+\) \(.*\)/- \2 (\1)/')
EOF
unix2dos commit_github.txt
notepad.exe commit_github.txt &
confirm "Did you remove non-visible commits and include non-thcrap commits?"

echo "Uploading the release on github..."
upload_url=$(jq -n --arg msg "$(cat commit_github.txt)" --arg date "$(date -d "$DATE" +%Y-%m-%d)" '{ "tag_name": $date, "name": $date, "body": $msg }' | \
curl -s 'https://api.github.com/repos/thpatch/thcrap/releases' -Lu "$GITHUB_LOGIN:$GITHUB_TOKEN" -H 'Content-Type: application/json' -d@- | \
jq -r .upload_url | sed -e 's/{.*}//')
if [ "$upload_url" == "null" ]; then echo "Releasing on GitHub failed."; fi

ret=$(curl -s "$upload_url?name=thcrap.zip" -Lu "$GITHUB_LOGIN:$GITHUB_TOKEN" -H 'Content-Type: application/zip' --data-binary @thcrap.zip | jq -r '.state')
if [ "$ret" != "uploaded" ]; then echo "thcrap.zip upload on GitHub failed."; fi
ret=$(curl -s "$upload_url?name=thcrap.zip.sig" -Lu "$GITHUB_LOGIN:$GITHUB_TOKEN" -H 'Content-Type: application/octet-stream' --data-binary @thcrap.zip.sig | jq -r '.state')
if [ "$ret" != "uploaded" ]; then echo "thcrap.zip.sig upload on GitHub failed."; fi
ret=$(curl -s "$upload_url?name=thcrap_symbols.zip" -Lu "$GITHUB_LOGIN:$GITHUB_TOKEN" -H 'Content-Type: application/zip' --data-binary @thcrap_symbols.zip | jq -r '.state')
if [ "$ret" != "uploaded" ]; then echo "thcrap_symbols.zip upload on GitHub failed."; fi

# Push update
scp root@kosuzu.thpatch.net:/var/www/thcrap_update.js .
jq --arg version "0x$(date -d "$DATE" +%Y%m%d)" --arg zip_fn "stable/thcrap.zip" '.stable.version = $version | .stable.latest = $zip_fn' thcrap_update.js > tmp.js
mv tmp.js thcrap_update.js
scp thcrap_update.js root@kosuzu.thpatch.net:/var/www/thcrap_update.js

echo "Releasing finished!"
