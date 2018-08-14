#!/bin/sh

# This script creates new tag for encoder SW and system model.
# It updates version number, commits and creates new tags in GIT
# Run this script in encoder/software/scripts

if ! [ -d ../../.git ]
then
    echo "Not a git repository"
    exit 1
fi

PRODUCT=h1
PRODUCT_MAIN_REPOSITORY=/afs/hantro.com/projects/gforce/git/h1_encoder

SWTAG_PREFIX=sw$PRODUCT
SYSTAG_PREFIX=sys$PRODUCT

VERSION_SOURCE=("../source/h264/H264EncApi.c" \
                "../source/vp8/vp8encapi.c" \
                "../source/jpeg/JpegEncApi.c" \
                "../source/camstab/vidstabapi.c")

ENCAPI=("H264ENC" \
        "VP8ENC" \
        "JPEGENC" \
        "VIDEOSTB")
        
DB_PASSWD="laboratorio"
DB_USER="db_user"
DB_HOST="172.28.107.116"
DB_NAME="testDBEnc"

MYSQL_OPTIONS="--skip-column-names"
DB_CMD="mysql ${MYSQL_OPTIONS} -h${DB_HOST} -u${DB_USER} -p${DB_PASSWD} -e"
DB_QUERY_ERROR="0"

dbQuery()
{
    local query=$1
   
    local db_result=`$DB_CMD "use $DB_NAME; ${query}"`
    DB_QUERY_ERROR="$?"
    echo "$db_result"
}

getUserID()
{
    local query="SELECT id FROM users WHERE handle = \"$USER\";"
    local db_result=$(dbQuery "$query")
    # is user not found, add $USER
    if [ -z "$db_result" ]
    then
        name=`finger "$USER" |grep -m 1 "$USER" |awk -F \Name: '{print $2}'| sed 's/^[ \t]*//;s/[ \t]*$//'`
        # if there is no user (which should not be possible)
        if [ -z "$name" ]
        then
            name="$USER"
        fi
        query="INSERT INTO users(id,handle,name) VALUES (NULL,\"$USER\", \"$name\"); SELECT LAST_INSERT_ID();"
        db_result=$(dbQuery "$query")
    fi
    echo "$db_result"
}

getProductID()
{
    local query="SELECT id FROM product WHERE name = UPPER(\"${PRODUCT}\");"
    local db_result=$(dbQuery "$query")
    echo "$db_result"
}

getSwTagId()
{
    local sw_tag=$1

    local query="SELECT id FROM sw_tag WHERE name = \"${sw_tag}\";"
    local db_result=$(dbQuery "$query")
    echo $db_result
}

insertSwTag()
{
    local sw_tag=$1

    local user_id=$(getUserID)
    local product_id=$(getProductID)
    if [ "$user_id" != "0" ]
    then
        local sw_tag_id=$(getSwTagId ${sw_tag})
        if [ -z "$sw_tag_id" ]
        then
            major=`echo "$sw_tag" | awk -F_ '{print $2}'`
            minor=`echo "$sw_tag" | awk -F_ '{print $3}'`
            local query="INSERT INTO sw_tag(id, name, created, tagger_id, product_id, major, minor) VALUES (NULL, \"${sw_tag}\", NULL, ${user_id}, ${product_id}, ${major}, ${minor});SELECT LAST_INSERT_ID();"
            local db_result=$(dbQuery "$query")
            sw_tag_id=${db_result}
        fi
        echo "$sw_tag_id"
    else
        echo "User id for $USER is not found in test database, contact DB admin (petriu)"
        exit
    fi
}

getSystemTagId()
{
    local system_tag=$1

    local query="SELECT id FROM system_tag WHERE name = \"${system_tag}\";"
    local db_result=$(dbQuery "$query")
    echo $db_result
}

insertSystemTag()
{
    local system_tag=$1

    local user_id=$(getUserID)
    local product_id=$(getProductID)
    if [ "$user_id" != "0" ]
    then
        local system_tag_id=$(getSystemTagId ${system_tag})
        if [ -z "$system_tag_id" ]
        then
            major=`echo "$system_tag" | awk -F_ '{print $2}'`
            minor=`echo "$system_tag" | awk -F_ '{print $3}'`
            local query="INSERT INTO system_tag(id, name, created, tagger_id, product_id, major, minor) VALUES (NULL, \"${system_tag}\", NULL, ${user_id}, ${product_id}, ${major}, ${minor});SELECT LAST_INSERT_ID();"
            local db_result=$(dbQuery "$query")
            system_tag_id=${db_result}
        fi
        echo "$system_tag_id"
    else
        echo "User id for $USER is not found in test database, contact DB admin (petriu)"
        exit
    fi
}

isdigit ()    # Tests whether *entire string* is numerical.
{             # In other words, tests for integer variable.
  [ $# -eq 1 ] || return 1

  case $1 in
  *[!0-9]*|"") return 1;;
            *) return 0;;
  esac
}

# check and list if there are any modified files in the project
res=`git diff 2>&1`

if [ "$res" != "" ]
then
    echo -e "WARNING! Found locally modified files!"
    echo "$res"
    echo ""
    echo -n "Continue? [y/n] "
    read ans
    if [ "$ans" != "y" ]
    then
        echo -e "\tNO, check the differences then!\nQuitting..."
        exit 1
    else
        echo -e "\tYES, but you have been warned!"
    fi
else
    echo -e "No locally modified files, good!"
fi

# check if API implementation is up-to-date; must be!
echo -e "\nChecking GIT status of \"${VERSION_SOURCE[0]}\""
git_diff=$(git diff ${VERSION_SOURCE[0]})

echo "$git_diff"

if [ "$git_diff" != "" ]
then
    echo -e "Please check in all changes first!\nQuitting..."
    exit 1
fi

echo "Your current branch is:"
git branch

prev_MAJOR=`sed -n -e 's/\(#define '${ENCAPI[0]}'_BUILD_MAJOR \)\([0-9]*\)/\2/p' ${VERSION_SOURCE[0]}`
prev_MINOR=`sed -n -e 's/\(#define '${ENCAPI[0]}'_BUILD_MINOR \)\([0-9]*\)/\2/p' ${VERSION_SOURCE[0]}`

# ask for new major and minor in command line
echo -e "\nUpdating version number and creating TAG for $PRODUCT software and system model."
echo -e "TAG will look like: ${SWTAG_PREFIX}_MAJOR_MINOR\n"

echo -e "Previous build number was: \t${prev_MAJOR}.$prev_MINOR"
echo -e "Please give new MAJOR and MINOR build numbers next.\n"

echo -n "MAJOR build number: "
read MAJOR

if ! isdigit "$MAJOR"
then
  echo -e "\"$MAJOR\" has at least one non-digit character.\nQuitting..."
  exit 1
fi

echo -en "\nMINOR build number: "
read MINOR

if ! isdigit "$MINOR"
then
  echo -e "\"$MINOR\" has at least one non-digit character.\nQuitting..."
  exit 1
fi

# our new TAG
SWTAGNAME=${SWTAG_PREFIX}_${MAJOR}_${MINOR}
SYSTAGNAME=${SYSTAG_PREFIX}_${MAJOR}_${MINOR}

# chck validity of new build version
if [ $(expr $prev_MAJOR \* 1000 + $prev_MINOR) -ge $(expr $MAJOR \* 1000 + $MINOR) ]
then
    echo "New version: ${MAJOR}.$MINOR NOT OK. Previous one: ${prev_MAJOR}.$prev_MINOR"
    exit 1
fi

echo -en "\nTAG to be created \"$SWTAGNAME\" and \"$SYSTAGNAME\". Is this OK? [y/n]: "
read ans

if [ "$ans" != "y" ]
then
    echo -e "TAG name was NOT confirmed to be OK! Type 'y' next time!\nQuitting..."
    exit 1
fi

# update new version number to every API source

for i in 0 1 2 3
do
    echo -e "\nUpdating \"${VERSION_SOURCE[$i]}\" with new build number..."

    sed -e 's/\(#define '${ENCAPI[$i]}'_BUILD_MAJOR \)\([0-9]*\)/\1'$MAJOR'/' \
        -e 's/\(#define '${ENCAPI[$i]}'_BUILD_MINOR \)\([0-9]*\)/\1'$MINOR'/' \
            ${VERSION_SOURCE[$i]} > foo.c

    echo "Previous version saved as: ${VERSION_SOURCE[$i]}.old"
    mv ${VERSION_SOURCE[$i]} ${VERSION_SOURCE[$i]}.old
    mv foo.c ${VERSION_SOURCE[$i]}
done

# commit new version numbers

echo -en "git commit\t"
if (git commit -m "build number update for tag $SWTAGNAME" \
    ${VERSION_SOURCE[0]} ${VERSION_SOURCE[1]} ${VERSION_SOURCE[2]} ${VERSION_SOURCE[3]} &> /dev/null)
then echo "OK"
else
    echo "FAILED"
    exit 1
fi

# and in the end do the tagging

echo -e "\nTagging component"
git tag -a $SWTAGNAME
insertSwTag "$SWTAGNAME"
git tag -a $SYSTAGNAME
insertSystemTag "$SYSTAGNAME"

echo -e "\nNew tags created. Now run tagtest.sh\n"

exit 0
