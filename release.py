#!/usr/bin/env python3

import argparse
import subprocess
import sys
import shutil

if shutil.which("git") is None:
    print("Error: git command not found. Is git installed and in your PATH?")
    sys.exit(1)

def get_current_branch():
    try:
        result = subprocess.run(["git", "rev-parse", "--abbrev-ref", "HEAD"], capture_output=True, text=True, check=True)
        return result.stdout.strip()
    except subprocess.CalledProcessError as e:
        print(f"Error getting current branch: {e}")
        if e.stderr:
            print(f"Stderr:\n{e.stderr.strip()}")
        return None

def create_tag(tag_name, message, push_commits):
    print(f"Creating tag: {tag_name}")
    if not message:
        message = f"Release {tag_name}"
        print(f"Using auto-generated message: '{message}'")

    try:
        if push_commits:
            current_branch = get_current_branch()
            if current_branch:
                print(f"Pushing commits on branch: {current_branch}")
                subprocess.run(["git", "push", "origin", current_branch], capture_output=True, text=True, check=True)
            else:
                print("Could not determine current branch, skipping commit push.")

        subprocess.run(["git", "tag", "-a", tag_name, "-m", message], capture_output=True, text=True, check=True)
        subprocess.run(["git", "push", "origin", tag_name], capture_output=True, text=True, check=True)
        print(f"Tag '{tag_name}' created and pushed with message: '{message}'.")
        return True
    except subprocess.CalledProcessError as e:
        print(f"Git command failed: {e}")
        if e.stderr:
            print(f"Stderr:\n{e.stderr.strip()}")
        return False

def delete_tag(tag_name):
    print(f"Deleting tag: {tag_name}")
    try:
        subprocess.run(["git", "push", "--delete", "origin", tag_name], capture_output=True, text=True, check=True)
        subprocess.run(["git", "tag", "--delete", tag_name], capture_output=True, text=True, check=True)
        print(f"Tag '{tag_name}' deleted.")
        return True
    except subprocess.CalledProcessError as e:
        print(f"Git command failed: {e}")
        if e.stderr:
            print(f"Stderr:\n{e.stderr.strip()}")
        return False

def main():
    parser = argparse.ArgumentParser(description="Manage git tags (create/delete).")
    subparsers = parser.add_subparsers(title="actions", dest="action", required=True)

    create_parser = subparsers.add_parser("create", help="Create and push a tag")
    create_parser.add_argument("tag_name", help="Tag name (e.g., v1.0.0-beta.1 or v2.5.3)")
    create_parser.add_argument("-m", "--message", help="Tag message (optional). Auto-generated from tag name if not provided.")
    create_parser.add_argument("-p", "--push-commits", action="store_true", help="Push commits on the current branch before pushing the tag.")

    delete_parser = subparsers.add_parser("delete", help="Delete a tag")
    delete_parser.add_argument("tag_name", help="Tag name to delete (e.g., v1.0.0-beta.1)")

    args = parser.parse_args()

    if args.action == "create":
        success = create_tag(args.tag_name, args.message, args.push_commits)
    elif args.action == "delete":
        success = delete_tag(args.tag_name)
    else:
        success = False

    if not success:
        sys.exit(1)

if __name__ == "__main__":
    main()
