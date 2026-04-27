#!/home/qchef/Documents/smash/smash

# Example SMASH Script
# Demonstrates scripting features

set SCRIPT_NAME="Demo Script"
export DEMO_VAR="test_value"

echo "=== SMASH Scripting Demo ==="
echo

echo "1. Variable Expansion:"
echo "   DEMO_VAR=$DEMO_VAR"
echo

echo "2. Current Directory:"
pwd
echo

echo "3. Creating Aliases:"
alias ll='ls -la'
alias showpwd='pwd'
echo "   Aliases created: ll, showpwd"
echo

echo "4. Environment:"
echo "   HOME=$HOME"
echo "   USER=$USER"
echo

echo "5. Command Execution:"
echo "   Files in current directory:"
ls -1 | grep smash
echo

echo "6. Redirection Test:"
echo "   Writing to temp file..."
echo "Hello from SMASH script" > /tmp/smash_test.txt
echo "   Reading back:"
cat /tmp/smash_test.txt
echo

echo "=== Demo Complete ==="
