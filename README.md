# Linux-system-programming

🕐 time — Date & Time CLI Utility
A lightweight command-line tool written in C that displays the current date and time in a clean, formatted output.


📋 Features
Print today's date in YYYY-MM-DD format
Print the current local time in HH:MM:SS format
Simple and intuitive command-line interface
Helpful error messages for invalid or missing commands


🛠️ Requirements
GCC or any C99-compatible compiler
Unix/Linux or macOS (uses standard C <time.h>)


📦 Installation
Clone the repository and compile with GCC:

git clone https://github.com/your-username/project.git

cd project

gcc -o project time.c


🚀 Usage
./project <command>
Commands
Command
Description
date
Print today's date
time
Print the current time
-h, --help
Show the help message

Examples
# Print today's date

./project date

# Output: Today's Date: 2026-04-17

# Print current time

./project time

# Output: Current Time: 14:35:22

# Show help

./project --help


⚠️ Error Handling
The tool handles the following error cases gracefully:

No command given — prints an error and exits with code 1
Unknown command — prints the invalid command name and suggests using -h


📁 Project Structure
project/

├── time.c      # Main source file

└── README.md   # Project documentation


📄 License
This project is open source and available under the MIT License.


