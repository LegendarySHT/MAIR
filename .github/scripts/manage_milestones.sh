#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

# --- Pre-check ---
# Ensure MAJOR, MINOR, PATCH are set
if [ -z "$MAJOR" ] || [ -z "$MINOR" ] || [ -z "$PATCH" ]; then
  echo "Error: MAJOR, MINOR, and PATCH environment variables must be set."
  exit 1
fi

# Ensure gh cli is logged in
if ! gh auth status > /dev/null 2>&1; then
  echo "Error: Not logged into GitHub. Please run 'gh auth login'."
  exit 1
fi

# Calculate the next version number
NEXT_MAJOR=$((MAJOR + 1))
NEXT_MINOR=$((MINOR + 1))
NEXT_PATCH=$((PATCH + 1))

# List of milestones to check/create
MILESTONES_TO_CREATE=(
  "${NEXT_MAJOR}.0.0"
  "${MAJOR}.${NEXT_MINOR}.0"
  "${MAJOR}.${MINOR}.${NEXT_PATCH}"
)


################################################################################
####                 Milestone creation                                     ####
################################################################################

# Fetch existing milestone titles using GitHub API
EXISTING_TITLES=$(gh api "repos/$GITHUB_REPOSITORY/milestones?state=all" --paginate --jq '.[].title')

# Create new milestones (if they don't exist)
echo "Checking for necessary milestones..."
for M in "${MILESTONES_TO_CREATE[@]}"; do
  if echo "$EXISTING_TITLES" | grep -Fxq "$M"; then
    echo "Milestone '$M' already exists, skipping creation."
  else
    echo "Creating milestone: $M"
    gh api "repos/$GITHUB_REPOSITORY/milestones" \
      -f title="$M" \
      -f state="open" \
      --silent # Use --silent to avoid verbose output
  fi
done
echo "Milestone check/creation complete."
echo "---"


################################################################################
####                 Milestone cleanup                                      ####
################################################################################

# Fetch all open milestones with their numbers and titles for easy lookup
echo "Fetching all open milestones for lookup..."
declare -A open_milestones
while IFS= read -r line; do
    number="${line%%:*}"
    title="${line#*:}"
    open_milestones["$title"]="$number"
done <<< "$(gh api "repos/$GITHUB_REPOSITORY/milestones" --paginate --jq '.[] | select(.state=="open") | "\(.number):\(.title)"')"
echo "Found ${#open_milestones[@]} open milestones."
echo "---"


# Delete all milestones with version less than the current closed milestone
# Current closed milestone version
CURRENT_VERSION="${MAJOR}.${MINOR}.${PATCH}"

# Version comparison function, returns 0 (true) if $1 < $2
version_lt() {
  # Use sort -V to compare semantic versions
  [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -n1)" = "$1" ] && [ "$1" != "$2" ]
}

echo "Start deleting open milestones with version less than $CURRENT_VERSION..."

# Iterate over a copy of the keys, as we might read the array again
for TITLE in "${!open_milestones[@]}"; do
  NUMBER=${open_milestones["$TITLE"]}
  
  # Skip milestones that are not in x.y.z format
  if [[ ! "$TITLE" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Skipping milestone '$TITLE' (not in semantic version format)."
    continue
  fi

  # Skip if not less than current version
  if ! version_lt "$TITLE" "$CURRENT_VERSION"; then
    continue
  fi

  echo "Processing old milestone: $TITLE (number: $NUMBER)"

  #################################################################################
  ####                 Move issues to the next milestone                       ####
  #################################################################################

  # 1. Determine the next milestone to move issues to
  NEXT_MILESTONE_TITLE=""
  if [[ "$TITLE" =~ ^[0-9]+\.0\.0$ ]]; then
    # Major release (e.g., 2.0.0) -> move to next major (e.g., 4.0.0)
    NEXT_MILESTONE_TITLE="${NEXT_MAJOR}.0.0"
  elif [[ "$TITLE" =~ ^[0-9]+\.[1-9][0-9]*\.0$ ]]; then
    # Minor release (e.g., 2.8.0) -> move to next minor (e.g., 3.1.0)
    NEXT_MILESTONE_TITLE="${MAJOR}.${NEXT_MINOR}.0"
  else
    # Patch release (e.g., 2.7.3) -> move to next patch (e.g., 3.0.1)
    NEXT_MILESTONE_TITLE="${MAJOR}.${MINOR}.${NEXT_PATCH}"
  fi
  
  NEXT_MILESTONE_NUMBER=${open_milestones["$NEXT_MILESTONE_TITLE"]}

  if [ -z "$NEXT_MILESTONE_NUMBER" ]; then
    echo "  [ERROR] Could not find the number for the next milestone '$NEXT_MILESTONE_TITLE'. Skipping issue migration for '$TITLE'."
    continue
  fi
  echo "  Target milestone for issues is '$NEXT_MILESTONE_TITLE' (number: $NEXT_MILESTONE_NUMBER)."

  # 2. Find all issues/PRs in the current milestone
  ISSUES_TO_MOVE=$(gh api "repos/$GITHUB_REPOSITORY/issues?milestone=$NUMBER&state=all&per_page=100" --paginate --jq '.[].number')

  if [ -z "$ISSUES_TO_MOVE" ]; then
    echo "  No issues or PRs found in milestone '$TITLE'."
  else
    echo "  Found issues/PRs to move: ${ISSUES_TO_MOVE//$'\n'/, }"
    # 3. Move each issue/PR to the next milestone
    for ISSUE_NUMBER in $ISSUES_TO_MOVE; do
      echo "    Moving issue #$ISSUE_NUMBER from milestone '$TITLE' to '$NEXT_MILESTONE_TITLE'..."
      gh api "repos/$GITHUB_REPOSITORY/issues/$ISSUE_NUMBER" \
        -X PATCH \
        -f milestone=$NEXT_MILESTONE_NUMBER \
        --silent
    done
  fi

  # 4. Delete the now-empty milestone
  echo "  Deleting milestone: $TITLE (number: $NUMBER)"
  gh api "repos/$GITHUB_REPOSITORY/milestones/$NUMBER" -X DELETE --silent
  echo "  Milestone '$TITLE' deleted successfully."
  echo "---"

done

echo "Milestone cleanup complete."
