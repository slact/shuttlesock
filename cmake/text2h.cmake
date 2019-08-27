include(CMakeParseArguments)

# Function to embed contents of a file as byte array in C/C++ header file(.h). The header file
# will contain a byte array and integer variable holding the size of the array.
# Parameters
#   SOURCE_FILE     - The path of source file whose contents will be embedded in the header file.
#   VARIABLE_NAME   - The name of the variable for the byte array. The string "_SIZE" will be append
#                     to this name and will be used a variable name for size variable.
#   HEADER_FILE     - The path of header file.
#   APPEND          - If specified appends to the header file instead of overwriting it
#   NULL_TERMINATE  - If specified a null byte(zero) will be append to the byte array. This will be
#                     useful if the source file is a text file and we want to use the file contents
#                     as string. But the size variable holds size of the byte array without this
#                     null byte.
# Usage:
#   text2h(SOURCE_FILE "Logo.png" HEADER_FILE "Logo.h" VARIABLE_NAME "LOGO_PNG")
function(TEXT2H)
    set(options APPEND NULL_TERMINATE)
    set(oneValueArgs SOURCE_FILE VARIABLE_NAME HEADER_FILE)
    cmake_parse_arguments(TEXT2H "${options}" "${oneValueArgs}" "" ${ARGN})

    # reads source file contents as text string
    file(READ ${TEXT2H_SOURCE_FILE} txtString)
    message("${txtString}")
    string(LENGTH "${txtString}" txtStringLength)

    # appends null byte if asked
    if(TEXT2H_NULL_TERMINATE)
        set(txtString "${txtString}\\x00")
    endif()

    # converts the variable name into proper C identifier
    string(MAKE_C_IDENTIFIER "${TEXT2H_VARIABLE_NAME}" TEXT2H_VARIABLE_NAME)
    string(TOUPPER "${TEXT2H_VARIABLE_NAME}" TEXT2H_VARIABLE_NAME)
    
    #escape quotes and newlines, and indent the newlines
    string(REGEX REPLACE "\\\\" "\\\\\\\\" txtString "${txtString}")
    string(CONFIGURE "\${txtString}" txtString ESCAPE_QUOTES)
    string(REGEX REPLACE "\n" "\\\\n\"\n  \"" txtString "${txtString}")
    
    # declares byte array and the length variables
    set(arrayDefinition "const unsigned char ${TEXT2H_VARIABLE_NAME}[] = \n  \"${txtString}\";")
    set(arraySizeDefinition "const size_t ${TEXT2H_VARIABLE_NAME}_SIZE = ${txtStringLength};")

    set(declarations "${arrayDefinition}\n${arraySizeDefinition}\n\n")
    if(TEXT2H_APPEND)
        file(APPEND ${TEXT2H_HEADER_FILE} "${declarations}")
    else()
        file(WRITE ${TEXT2H_HEADER_FILE} "${declarations}")
    endif()
endfunction()
