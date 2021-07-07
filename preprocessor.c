#include "compiler.h"
#include "helpers/vector.h"

enum
{
    PREPROCESSOR_NUMBER_NODE,
    PREPROCESSOR_IDENTIFIER_NODE,
    PREPROCESSOR_EXPRESSION_NODE,
    PREPROCESSOR_PARENTHESES_NODE
};

struct preprocessor_node
{
    int type;
    struct preprocessor_const_val
    {
        union
        {
            char cval;
            const char *sval;
            unsigned int inum;
            long lnum;
            long long llnum;
            unsigned long ulnum;
            unsigned long long ullnum;
        };
    } const_val;

    union
    {
        struct preprocessor_exp_node
        {
            struct preprocessor_node *left;
            struct preprocessor_node *right;
            const char *op;
        } exp;

        /**
         * Represents a parenthesis expression i.e (50+20) (EXP)
         */
        struct preprocessor_parenthesis
        {
            // The expression between the brackets ()
            struct node *exp;
        } parenthesis;
    };

    const char *sval;
};

struct preprocessor_function_argument
{
    // Tokens for this argument struct token
    struct vector *tokens;
};

struct preprocessor_function_arguments
{
    // Vector of struct preprocessor_function_argument
    struct vector *arguments;
};

void preprocessor_handle_token(struct compile_process *compiler, struct token *token);

struct preprocessor_function_arguments *preprocessor_function_arguments_create()
{
    struct preprocessor_function_arguments *args = calloc(sizeof(struct preprocessor_function_arguments), 1);
    args->arguments = vector_create(sizeof(struct preprocessor_function_argument));
    return args;
}

int preprocessor_function_arguments_count(struct preprocessor_function_arguments *arguments)
{
    return vector_count(arguments->arguments);
}

struct vector *preprocessor_function_arguments_vector(struct preprocessor_function_arguments *arguments)
{
    return arguments->arguments;
}

void preprocessor_function_argument_free(struct preprocessor_function_argument *argument)
{
    vector_free(argument->tokens);
}

void preprocessor_function_arguments_free(struct preprocessor_function_arguments *arguments)
{
    vector_set_peek_pointer(arguments->arguments, 0);
    struct preprocessor_function_argument *argument = vector_peek(arguments->arguments);
    while (argument)
    {
        preprocessor_function_argument_free(argument);
        argument = vector_peek(arguments->arguments);
    }

    free(arguments);
}

void preprocessor_function_argument_push(struct preprocessor_function_arguments *arguments, struct vector *value_vec)
{
    struct preprocessor_function_argument arg;
    arg.tokens = vector_clone(value_vec);
    vector_push(arguments->arguments, &arg);
}

void *preprocessor_node_create(struct preprocessor_node *node)
{
    struct preprocessor_node *result = calloc(sizeof(struct preprocessor_node), 1);
    memcpy(result, node, sizeof(struct preprocessor_node));
    return result;
}

struct preprocessor *compiler_preprocessor(struct compile_process *compiler)
{
    return compiler->preprocessor;
}
static struct token *preprocessor_next_token(struct compile_process *compiler)
{
    return vector_peek(compiler->token_vec_original);
}

static struct token *preprocessor_next_token_no_increment(struct compile_process *compiler)
{
    return vector_peek_no_increment(compiler->token_vec_original);
}

static void preprocessor_token_push_dst(struct compile_process *compiler, struct token *token)
{
    struct token t = *token;
    vector_push(compiler->token_vec, &t);
}

void preprocessor_token_vec_push_dst(struct compile_process *compiler, struct vector *token_vec)
{
    vector_set_peek_pointer(token_vec, 0);
    struct token *token = vector_peek(token_vec);
    while (token)
    {
        vector_push(compiler->token_vec, token);
        token = vector_peek(token_vec);
    }
}

static bool preprocessor_token_is_preprocessor_keyword(struct token *token)
{
    return token->type == TOKEN_TYPE_IDENTIFIER &&
           (S_EQ(token->sval, "define") || S_EQ(token->sval, "if") || S_EQ(token->sval, "ifdef") || S_EQ(token->sval, "ifndef") || S_EQ(token->sval, "endif"));
}

static bool preprocessor_token_is_define(struct token *token)
{
    if (!preprocessor_token_is_preprocessor_keyword(token))
    {
        return false;
    }

    return (S_EQ(token->sval, "define"));
}

static bool preprocessor_token_is_ifdef(struct token *token)
{
    if (!preprocessor_token_is_preprocessor_keyword(token))
    {
        return false;
    }

    return (S_EQ(token->sval, "ifdef"));
}

/**
 * Searches for a hashtag symbol along with the given identifier.
 * If found the hashtag token and identifier token are both popped from the stack
 * Only the target identifier token representing "str" is returned.
 * 
 * NO match return null.
 */
static struct token *preprocessor_hashtag_and_identifier(struct compile_process *compiler, const char *str)
{
    // No token then how can we continue?
    if (!preprocessor_next_token_no_increment(compiler))
        return NULL;

    if (!token_is_symbol(preprocessor_next_token_no_increment(compiler), '#'))
    {
        return NULL;
    }

    vector_save(compiler->token_vec_original);
    // Ok skip the hashtag symbol
    preprocessor_next_token(compiler);

    struct token *target_token = preprocessor_next_token_no_increment(compiler);
    if (token_is_identifier(target_token, str))
    {
        // Pop off the target token
        preprocessor_next_token(compiler);
        // Purge the vector save
        vector_save_purge(compiler->token_vec_original);
        return target_token;
    }

    vector_restore(compiler->token_vec_original);

    return NULL;
}

struct preprocessor_definition *preprocessor_get_definition(struct preprocessor *preprocessor, const char *name)
{
    vector_set_peek_pointer(preprocessor->definitions, 0);
    struct preprocessor_definition *definition = vector_peek_ptr(preprocessor->definitions);
    while (definition)
    {
        if (S_EQ(definition->name, name))
        {
            break;
        }
        definition = vector_peek_ptr(preprocessor->definitions);
    }

    return definition;
}

static bool preprocessor_token_is_definition_identifier(struct compile_process *compiler, struct token *token)
{
    if (token->type != TOKEN_TYPE_IDENTIFIER)
        return false;

    if (preprocessor_get_definition(compiler->preprocessor, token->sval))
    {
        return true;
    }

    return false;
}

struct preprocessor_definition *preprocessor_definition_create(const char *name, struct vector *value_vec, struct vector *arguments)
{
    struct preprocessor_definition *definition = calloc(sizeof(struct preprocessor_definition), 1);
    definition->type = PREPROCESSOR_DEFINITION_STANDARD;
    definition->name = name;
    definition->value = value_vec;
    definition->arguments = arguments;

    if (vector_count(definition->arguments))
    {
        definition->type = PREPROCESSOR_DEFINITION_MACRO_FUNCTION;
    }

    return definition;
}

void preprocessor_multi_value_insert_to_vector(struct compile_process *compiler, struct vector *value_token_vec)
{
    struct token *value_token = preprocessor_next_token(compiler);
    while (value_token)
    {
        if (value_token->type == TOKEN_TYPE_NEWLINE)
        {
            break;
        }

        if (token_is_symbol(value_token, '\\'))
        {
            // This allows for another line
            // Skip new line
            preprocessor_next_token(compiler);
            value_token = preprocessor_next_token(compiler);
            continue;
        }

        vector_push(value_token_vec, value_token);
        value_token = preprocessor_next_token(compiler);
    }
}
static void preprocessor_handle_definition_token(struct compile_process *compiler)
{
    struct token *name_token = preprocessor_next_token(compiler);

    // Arguments vector in case this definition has function arguments
    struct vector *arguments = vector_create(sizeof(const char *));
    // Do we have a function definition
    if (token_is_operator(preprocessor_next_token_no_increment(compiler), "("))
    {
        preprocessor_next_token(compiler);
        struct token *next_token = preprocessor_next_token(compiler);
        while (!token_is_symbol(next_token, ')'))
        {
            if (next_token->type != TOKEN_TYPE_IDENTIFIER)
            {
                FAIL_ERR("You must provide an identifier in the preprocessor definition");
            }

            // Save the argument for later.
            vector_push(arguments, (void *)next_token->sval);

            next_token = preprocessor_next_token(compiler);
            if (!token_is_operator(next_token, ",") && !token_is_symbol(next_token, ')'))
            {
                FAIL_ERR("Incomplete sequence for macro arguments");
            }

            if (token_is_symbol(next_token, ')'))
            {
                break;
            }

            // Skip the operator ","
            next_token = preprocessor_next_token(compiler);
        }
    }
    // Value can be composed of many tokens
    struct vector *value_token_vec = vector_create(sizeof(struct token));
    preprocessor_multi_value_insert_to_vector(compiler, value_token_vec);

    struct preprocessor *preprocessor = compiler->preprocessor;
    struct preprocessor_definition *definition = preprocessor_definition_create(name_token->sval, value_token_vec, arguments);
    vector_push(preprocessor->definitions, &definition);
}

static void preprocessor_handle_ifdef_token(struct compile_process *compiler)
{
    struct token *condition_token = preprocessor_next_token(compiler);
    if (!condition_token)
    {
        FAIL_ERR("No condition token provided????. WIll replace later with proper error system");
    }

    // Let's see if we have a definition of the condition token name
    struct preprocessor_definition *definition = preprocessor_get_definition(compiler_preprocessor(compiler), condition_token->sval);

    // We have this definition we can proceed with the rest of the body, until
    // an #endif is discovered
    while (preprocessor_next_token_no_increment(compiler) && !preprocessor_hashtag_and_identifier(compiler, "endif"))
    {
        if (definition)
        {
            preprocessor_handle_token(compiler, preprocessor_next_token(compiler));
            continue;
        }

        // Skip the unexpected token
        preprocessor_next_token(compiler);
    }
}

void preprocessor_handle_identifier_macro_call_argument(struct preprocessor_function_arguments *arguments, struct vector *token_vec)
{
    preprocessor_function_argument_push(arguments, token_vec);
}

static struct token *preprocessor_handle_identifier_macro_call_argument_parse(struct compile_process *compiler, struct vector *value_vec, struct preprocessor_function_arguments *arguments, struct token *token)
{
    if (token_is_symbol(token, ')'))
    {
        // We are done handle the call argument
        preprocessor_handle_identifier_macro_call_argument(arguments, value_vec);
        return NULL;
    }

    if (token_is_operator(token, ","))
    {
        preprocessor_handle_identifier_macro_call_argument(arguments, value_vec);
        // Clear the value vector ready for the next argument
        vector_clear(value_vec);
        token = preprocessor_next_token(compiler);
        return token;
    }

    // OK this token is important push it to the value vector
    vector_push(value_vec, token);

    token = preprocessor_next_token(compiler);
    return token;
}

/**
 * Returns the index of the argument or -1 if not found.
 */
int preprocessor_definition_argument_exists(struct preprocessor_definition *definition, const char *name)
{
    vector_set_peek_pointer(definition->arguments, 0);
    int i = 0;
    const char *current = vector_peek(definition->arguments);
    while (current)
    {
        if (S_EQ(current, name))
            return i;

        i++;
        current = vector_peek(definition->arguments);
    }

    return -1;
}

struct preprocessor_function_argument *preprocessor_function_argument_at(struct preprocessor_function_arguments *arguments, int index)
{
    struct preprocessor_function_argument *argument = vector_at(arguments->arguments, index);
    return argument;
}

void preprocessor_function_argument_push_to_vec(struct preprocessor_function_argument *argument, struct vector *vector_out)
{
    vector_set_peek_pointer(argument->tokens, 0);
    struct token *token = vector_peek(argument->tokens);
    while (token)
    {
        vector_push(vector_out, token);
        token = vector_peek(argument->tokens);
    }
}
void preprocessor_macro_function_execute(struct compile_process *compiler, const char *function_name, struct preprocessor_function_arguments *arguments)
{
    struct preprocessor *preprocessor = compiler_preprocessor(compiler);
    struct preprocessor_definition *definition = preprocessor_get_definition(preprocessor, function_name);
    if (!definition)
    {
        FAIL_ERR("Definition was not found");
    }

    if (definition->type != PREPROCESSOR_DEFINITION_MACRO_FUNCTION)
    {
        FAIL_ERR("This definition is not a macro function");
    }

    if (vector_count(definition->arguments) != preprocessor_function_arguments_count(arguments))
    {
        FAIL_ERR("You passed too many arguments to this macro functon, expecting %i arguments");
    }

    // Let's create a special vector for this value as its being injected with function arugments
    struct vector *value_vec_target = vector_create(sizeof(struct token));
    vector_set_peek_pointer(definition->value, 0);
    struct token *token = vector_peek(definition->value);
    while (token)
    {
        if (token->type == TOKEN_TYPE_IDENTIFIER)
        {
            // Let's check if we have a function argument in the definition
            // if so this needs replacing
            int argument_index = preprocessor_definition_argument_exists(definition, token->sval);
            if (argument_index != -1)
            {
                // Ok we have an argument, we need to populate the output vector
                // a little differently.
                preprocessor_function_argument_push_to_vec(preprocessor_function_argument_at(arguments, argument_index), value_vec_target);
                token = vector_peek(definition->value);
                continue;
            }
        }
        // Push the token it does not need modfiying
        vector_push(value_vec_target, token);
        token = vector_peek(definition->value);
    }

    // We have our target vector, lets inject it into the output vector.
    preprocessor_token_vec_push_dst(compiler, value_vec_target);
   // vector_free(value_vec_target);
}
static struct preprocessor_function_arguments *preprocessor_handle_identifier_macro_call_arguments(struct compile_process *compiler)
{
    // Skip the left bracket
    preprocessor_next_token(compiler);

    // We need room to store these arguments
    struct preprocessor_function_arguments *arguments = preprocessor_function_arguments_create();

    // Ok lets loop through all the values to form a function call argument vector
    struct token *token = preprocessor_next_token(compiler);
    struct vector *value_vec = vector_create(sizeof(struct token));
    while (token)
    {
        token = preprocessor_handle_identifier_macro_call_argument_parse(compiler, value_vec, arguments, token);
    }

    // Free the now unused value vector
    vector_free(value_vec);
    return arguments;
}

static void preprocessor_handle_identifier(struct compile_process *compiler, struct token *token)
{
    // We have an identifier, it could represent a variable or a definition
    // lets check if its a definition if so we have to handle it.

    struct preprocessor_definition *definition = preprocessor_get_definition(compiler->preprocessor, token->sval);
    if (!definition)
    {
        // Not our token then it belongs on the destination token vector.
        preprocessor_token_push_dst(compiler, token);
        return;
    }

    // We have a defintion, is this a function call macro
    if (token_is_operator(preprocessor_next_token_no_increment(compiler), "("))
    {
        // Let's create a vector for these arguments
        struct preprocessor_function_arguments *arguments = preprocessor_handle_identifier_macro_call_arguments(compiler);
        const char *function_name = token->sval;
        // Let's execute the macro function
        preprocessor_macro_function_execute(compiler, function_name, arguments);
        // preprocessor_function_arguments_free(arguments);
        return;
    }

    // Normal macro function, then push its entire value stack to the destination stack
    preprocessor_token_vec_push_dst(compiler, definition->value);
}

static int preprocessor_handle_hashtag_token(struct compile_process *compiler, struct token *token)
{
    bool is_preprocessed = false;
    struct token *next_token = preprocessor_next_token(compiler);
    if (preprocessor_token_is_define(next_token))
    {
        preprocessor_handle_definition_token(compiler);
        is_preprocessed = true;
    }
    else if (preprocessor_token_is_ifdef(next_token))
    {
        preprocessor_handle_ifdef_token(compiler);
        is_preprocessed = true;
    }

    return is_preprocessed;
}

static void preprocessor_handle_symbol(struct compile_process *compiler, struct token *token)
{
    int is_preprocessed = false;
    if (token->cval == '#')
    {
        is_preprocessed = preprocessor_handle_hashtag_token(compiler, token);
    }

    // The symbol was not preprocessed so just push it to the destination stack.
    if (!is_preprocessed)
    {
        preprocessor_token_push_dst(compiler, token);
    }
}
void preprocessor_handle_token(struct compile_process *compiler, struct token *token)
{
    switch (token->type)
    {
    case TOKEN_TYPE_SYMBOL:
    {
        preprocessor_handle_symbol(compiler, token);
    }
    break;

    case TOKEN_TYPE_IDENTIFIER:
        preprocessor_handle_identifier(compiler, token);
        break;

    // Ignore new lines that we dont care for.
    case TOKEN_TYPE_NEWLINE:
        break;

    default:
        preprocessor_token_push_dst(compiler, token);
    }
}

void *preprocessor_handle_number_token(struct expressionable *expressionable)
{
    struct token *token = expressionable_token_next(expressionable);
    return preprocessor_node_create(&(struct preprocessor_node){.type = PREPROCESSOR_NUMBER_NODE, .const_val.llnum = token->llnum});
}

void *preprocessor_handle_identifier_token(struct expressionable *expressionable)
{
    struct token *token = expressionable_token_next(expressionable);
    return preprocessor_node_create(&(struct preprocessor_node){.type = PREPROCESSOR_IDENTIFIER_NODE, .sval = token->sval});
}

void preprocessor_make_expression_node(struct expressionable *expressionable, void *left_node_ptr, void *right_node_ptr, const char *op)
{
    struct preprocessor_node exp_node;
    exp_node.exp.left = left_node_ptr;
    exp_node.exp.right = right_node_ptr;
    exp_node.exp.op = op;

    expressionable_node_push(expressionable, preprocessor_node_create(&exp_node));
}

void preprocessor_make_parentheses_node(struct expressionable *expressionable, void *node_ptr)
{
    struct preprocessor_node *node = node_ptr;
    struct preprocessor_node parentheses_node;
    parentheses_node.type = PREPROCESSOR_PARENTHESES_NODE;
    parentheses_node.parenthesis.exp = node_ptr;
    expressionable_node_push(expressionable, preprocessor_node_create(&parentheses_node));
}

void *preprocessor_get_left_node(struct expressionable *expressionable, void *target_node)
{
    struct preprocessor_node *node = target_node;
    return node->exp.left;
}

void *preprocessor_get_right_node(struct expressionable *expressionable, void *target_node)
{
    struct preprocessor_node *node = target_node;
    return node->exp.right;
}

int preprocessor_get_node_type(struct expressionable *expressionable, void *node)
{
    int generic_type = EXPRESSIONABLE_GENERIC_TYPE_NON_GENERIC;
    struct preprocessor_node *preprocessor_node = node;
    switch (preprocessor_node->type)
    {
    case PREPROCESSOR_NUMBER_NODE:
        generic_type = EXPRESSIONABLE_GENERIC_TYPE_NUMBER;
        break;

    case PREPROCESSOR_IDENTIFIER_NODE:
        generic_type = EXPRESSIONABLE_GENERIC_TYPE_IDENTIFIER;
        break;

    case PREPROCESSOR_EXPRESSION_NODE:
        generic_type = EXPRESSIONABLE_GENERIC_TYPE_EXPRESSION;
        break;

    case PREPROCESSOR_PARENTHESES_NODE:
        generic_type = EXPRESSIONABLE_GENERIC_TYPE_PARENTHESES;
        break;
    }
    return preprocessor_node->type;
}

const char *preprocessor_get_node_operator(struct expressionable *expressionable, void *target_node)
{
    struct preprocessor_node *preprocessor_node = target_node;
    return preprocessor_node->exp.op;
}

void **preprocessor_get_left_node_address(struct expressionable *expressionable, void *target_node)
{
    struct preprocessor_node *node = target_node;
    return (void **)&node->exp.left;
}

void **preprocessor_get_right_node_address(struct expressionable *expressionable, void *target_node)
{
    struct preprocessor_node *node = target_node;
    return (void **)&node->exp.right;
}

void preprocessor_set_expression_node(struct expressionable *expressionable, void *node, void *left_node, void *right_node, const char *op)
{
    struct preprocessor_node *preprocessor_node = node;
    preprocessor_node->exp.left = left_node;
    preprocessor_node->exp.right = left_node;
    preprocessor_node->exp.op = op;
}

void preprocessor_initialize(struct vector *token_vec, struct preprocessor *preprocessor)
{
    memset(preprocessor, 0, sizeof(struct preprocessor));
    preprocessor->definitions = vector_create(sizeof(struct preprocessor_definition));
    struct expressionable_config config;
    config.callbacks.get_left_node = preprocessor_get_left_node;
    config.callbacks.get_left_node_address = preprocessor_get_left_node_address;
    config.callbacks.get_node_operator = preprocessor_get_node_operator;
    config.callbacks.get_node_type = preprocessor_get_node_type;
    config.callbacks.get_right_node = preprocessor_get_right_node;
    config.callbacks.get_right_node_address = preprocessor_get_right_node_address;
    config.callbacks.handle_identifier_callback = preprocessor_handle_identifier_token;
    config.callbacks.handle_number_callback = preprocessor_handle_number_token;
    config.callbacks.make_expression_node = preprocessor_make_expression_node;
    config.callbacks.make_parentheses_node = preprocessor_make_parentheses_node;
    config.callbacks.set_exp_node = preprocessor_set_expression_node;

    preprocessor->exp_vector = vector_create(sizeof(struct preprocessor_node *));
    preprocessor->expressionable = expressionable_create(&config, token_vec, NULL);
}

struct preprocessor *preprocessor_create(struct vector *token_vec)
{
    struct preprocessor *preprocessor = calloc(sizeof(struct preprocessor), 1);
    preprocessor_initialize(token_vec, preprocessor);
    return preprocessor;
}

int preprocessor_run(struct compile_process *compiler, const char *file)
{
    vector_set_peek_pointer(compiler->token_vec_original, 0);
    struct token *token = preprocessor_next_token(compiler);
    while (token)
    {
        preprocessor_handle_token(compiler, token);
        token = preprocessor_next_token(compiler);
    }

    // We are done? great we dont need the original token vector anymore, lets swap it out
    // for our one
    return 0;
}
