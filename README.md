
[![Build Status](https://travis-ci.org/florpor/simulator.png?branch=master)](https://travis-ci.org/florpor/simulator)

#Developer’s Guide

##Sign In using GitHub credentials
Follow the instructions at [Getting started with GerritHub](https://review.gerrithub.io/static/intro.html)

1. [Sign In](https://review.gerrithub.io/login) with your GitHub username and password.
2. Allow GerritHub accessing your profile
3. Select the SSH Public Keys you want to import


##Installing git-review
We recommend using the git-review tool which is a git subcommand that handles all the details of
working with Gerrit. On Ubuntu Precise (12.04) and later install it as any other package:
```
apt-get install git-review
```

On CentOS 6.5 and later install it by:
```
yum install git-review
```

##Starting Work on a New Project
Clone the [gerrit repository](https://review.gerrithub.io/#/admin/projects/davidsaOpenu/simulator) using ssh

##Starting a Change
Once your local repository is set up as above, you must use the following workflow
1. cd <projectname>
2. Make sure you have the latest upstream changes by *git pull*

##Committing a Change
Make your changes, commit them, and submit them for review:
```
git commit -a
```

Commit messages should start with a short 50 character or less summary in a single paragraph.
The following paragraph(s) should explain the change in more detail describing a propblem solved
by the change and the way it was solved.

For example:
```
Fixing ssd.conf parameters calculation based on disk size

Reducing disk size was ac heaved by recalculating flash number.
Which is wrong because this number can't drop too much.
Block number should be recalculated based on desired disk size.
```

If your changes addresses a blueprint or a bug, be sure to mention them in the commit message
using the following syntax:
```
Implements: blueprint BLUEPRINT
Closes-Bug: ####### (Partial-Bug or Related-Bug are options)
```

##Previewing a Change
Before submitting your change, you should make sure that your change does not contain the files
or lines you do not explicitly change:
```
git show
```

##Submitting a Change for Review
Once you have committed a change to your local repository, all you need to do to send it to Gerrit
for code review is run:
```
git review
```

##Updating a Change
If the code review process suggests additional changes, make and amend the changes to the existing commit.
Leave the existing Change-Id: footer in the commit message as-is. Gerrit knows that this is an updated
patchset for an existing change:
```
git commit -a --amend
git review
```

