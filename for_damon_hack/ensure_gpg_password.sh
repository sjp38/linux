#!/bin/bash

gpg-connect-agent updatestartuptty /bye > /dev/null
ssh gitolite.kernel.org help > /dev/null
ssh git@github.com &> /dev/null
