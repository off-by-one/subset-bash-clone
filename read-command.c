// UCLA CS 111 Lab 1 command reading

// Copyright 2012-2014 Paul Eggert.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

//#include "command.h"
//#include "command-internals.h"

#include <error.h>
#include <string.h>
#include <error.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include "command.h"
#include "command-internals.h"

#define DEBUG_SHELLER 0
#define TEST_SUITE 0

#define NOT_USED(x) ( (void)(x) )

char *__mem_store;

enum
{
  LEX_STATE_COUNT = 5,
  LEX_EVENT_COUNT = 5,
  TOKEN_REPO_SIZE = 500,
};

typedef enum
  {
    in_word = 0,
    in_special,
    in_newline,
    in_whitespace,
    in_comment,
  }
lex_state_t;

typedef enum 
  {
    regular_char = 0,
    special_char,
    newline_char,
    whitespace_char,
    comment_char,
  }
lex_event_t;

typedef struct
{
  char *array;
  int array_size_cap;
  int array_size;
}
token_repo_t;


typedef struct token_node
  {
    char *word;
    struct token_node *next;
  }
lex_token_t;

typedef lex_token_t* (*__lex_action_t) (token_repo_t*, lex_token_t*, int);
  

typedef struct lex_tokenizer_transition
{
  lex_state_t transition_state;
  __lex_action_t transition_action;
}
__lex_trans_t;

lex_token_t* append_to (token_repo_t* in,
    lex_token_t* token,  int byte);
lex_token_t* append_to_new (token_repo_t* in,
    lex_token_t* token, int byte);
lex_token_t* create_new (token_repo_t* in,
    lex_token_t* token, int byte);
lex_token_t* ignore(token_repo_t* in,
    lex_token_t* token, int byte);

__lex_trans_t lexical_transition_table[LEX_STATE_COUNT][LEX_EVENT_COUNT] = 
{
  {{in_word, &append_to},
     {in_special, &append_to_new},
     {in_newline, &append_to_new},
     {in_whitespace, &create_new},
     {in_word, &append_to}},

   {{in_word, &append_to_new},
     {in_special, &append_to_new},
     {in_newline, &append_to_new},
     {in_whitespace, &create_new},
     {in_comment, &create_new}},

  {{in_word, &append_to_new},
     {in_special, &append_to_new},
     {in_newline, &ignore},
     {in_whitespace, &create_new},
     {in_comment, &create_new}},

  {{in_word, &append_to},
     {in_special, &append_to},
     {in_newline, &append_to},
     {in_whitespace, &ignore},
     {in_comment, &ignore}},

  {{in_comment, &ignore},
     {in_comment, &ignore},
     {in_newline, &append_to},
     {in_comment, &ignore},
     {in_comment, &ignore}}
};

void resize_repo(token_repo_t* r, lex_token_t* head)
{
  char* array = r->array;

  r->array_size_cap = 2 * r->array_size_cap;

  r->array = realloc(r->array, r->array_size_cap);

  if (r->array == NULL)
  {
    fprintf(stderr, "error: script too large\n");
    exit(1);
  }

  if (array != r->array)
  {
    lex_token_t *p = head;
    while(p != NULL)
    {
      p->word = (p->word - array) + r->array;
      p = p->next;
    }
  }

}

lex_event_t char_to_event(char byte)
{
  char reg[] = "!%+,-./:@^_";
  int  regl = 11;
  char spec[] = ";|()<>";
  int specl = 6;
  int  i = 0;

  if (byte == '#') return comment_char;
  if (byte == '\n')                return newline_char;
  if (byte == ' ' || byte == '\t') return whitespace_char;

  /* account for non-alphanum regular characters */
  if (isalnum(byte))               return regular_char;
  for (i = 0; i < regl ; i++) if (byte == reg [i]) return regular_char;
  for (i = 0; i < specl; i++) if (byte == spec[i]) return special_char;

  fprintf(stderr, "error: unsupported character %c\n", byte);
  exit(1);
}


token_repo_t * create_token_repo()
{
  token_repo_t *r = malloc(sizeof(token_repo_t));

  r->array_size_cap = TOKEN_REPO_SIZE;
  r->array_size = 0;
  r->array = malloc(sizeof(char) * r->array_size_cap);

  return r;
}

lex_token_t* append_to (token_repo_t* in,
    lex_token_t* token,  int byte)
{
  in->array[in->array_size] = byte;
  in->array_size++;
  return token;
}

lex_token_t* append_to_new (token_repo_t* in,
    lex_token_t* token, int byte)
{
  token = create_new(in, token, byte);

  return append_to(in, token, byte);
}

lex_token_t* create_new (token_repo_t* in,
    lex_token_t* token, int byte)
{
  NOT_USED(byte);

  in->array[in->array_size] = '\0';
  in->array_size++;

  token->next = (lex_token_t*) malloc(sizeof(lex_token_t));

  token = token->next;
  token->next = NULL;
  token->word = (in->array + in->array_size);

  return token;
}

lex_token_t* ignore(token_repo_t* in,
    lex_token_t* token, int byte)
{
  NOT_USED(in);
  NOT_USED(byte);
  return token;
}

/* externally-called function */

lex_token_t * get_tokens (int (*get_next_byte) (void *),
		     void *get_next_byte_argument)
{
  token_repo_t * r = create_token_repo();

  lex_token_t* first_token = malloc(sizeof(lex_token_t));
  lex_token_t* this_token = first_token;

  this_token->word = r->array;

  int byte = '\0';
  lex_state_t state = in_whitespace;
  lex_event_t event = regular_char;


  while ( (byte = get_next_byte(get_next_byte_argument)) != EOF )
  {
    if (r->array_size > (r->array_size_cap - 1))
      resize_repo(r, first_token);

    event = char_to_event(byte);

    this_token = lexical_transition_table[state][event].transition_action
      (r, this_token, byte);

    state = lexical_transition_table[state][event].transition_state;
  }

  r->array[r->array_size] = 0;
  r->array_size++;

  return first_token;
}

char semicolon[] = ";";

typedef enum
{
  // primary command tokens
  if_token,
  while_token,
  until_token,
  simple_cmd_token,

  // the two subshell sides
  shell_left_token,
  shell_right_token,

  // ssequencing tokens
  pipe_token,
  sequence_token,
  newline_token,

  // conditional extras
  then_token,
  else_token,
  fi_token,
  do_token,
  done_token,

  // redirects
  direct_in_token,
  direct_out_token,
}
sh_class_t;

typedef struct shell_token_node
{
  sh_class_t type;

  union
  {
    // For all others
    char *word;

    // For SIMPLE_COMMAND
    char **words;
  };
  
  struct shell_token_node* next;
}
sh_token_t;

sh_class_t type_of_token(lex_token_t* token)
{
  if ( char_to_event( *(token->word) ) == regular_char )
  {
    if ( !strcmp(token->word, "if") )
      return if_token;
    else if ( !strcmp(token->word, "then") )
      return then_token;
    else if ( !strcmp(token->word, "else") )
      return else_token;
    else if ( !strcmp(token->word, "fi") )
      return fi_token;
    else if ( !strcmp(token->word, "until") )
      return until_token;
    else if ( !strcmp(token->word, "while") )
      return while_token;
    else if ( !strcmp(token->word, "do") )
      return do_token;
    else if ( !strcmp(token->word, "done") )
      return done_token;
    else
      return simple_cmd_token;
  }

  if ( !strcmp(token->word, "|") )
    return pipe_token;
  else if ( !strcmp(token->word, ";") )
    return sequence_token;
  else if ( !strcmp(token->word, "\n") )
    return newline_token;
  else if ( !strcmp(token->word, "(") )
    return shell_left_token;
  else if ( !strcmp(token->word, ")") )
    return shell_right_token;
  else if ( !strcmp(token->word, ">") )
    return direct_out_token;
  else if ( !strcmp(token->word, "<") )
    return direct_in_token;

  fprintf(stderr, "error: Script contains invalid token: %s\n", token->word);
  exit(1);
}

int length_of_simple_command(lex_token_t *token)
{
  int count = 0; // assume start on valid start word
  char c = 0;

  lex_token_t *iter_token = token;

  for(; iter_token != NULL; iter_token = iter_token->next)
  {
    c = *(iter_token->word);
    if (type_of_token(iter_token) == simple_cmd_token)
      count++;
    else
      break;
  }

  return count;
}

int following_newline_is_ignored(sh_class_t type)
{
  return  (type == if_token ||
           type == while_token ||
           type == until_token ||
           type == shell_left_token ||
           type == then_token ||
           type == else_token ||
           type == do_token ||
           type == pipe_token);
}

int following_newline_is_sequence(sh_class_t type)
{
  return ( type == simple_cmd_token ||
           type == shell_right_token ||
           type == fi_token ||
           type == done_token ||
           type == direct_in_token ||
           type == direct_out_token ) ;
}

sh_token_t* get_shell_tokens(lex_token_t* head_lex)
{
  lex_token_t *p_lex = head_lex;

  sh_token_t *head_sh = malloc(sizeof(sh_token_t));
  sh_token_t *p_sh = head_sh;
  sh_class_t prev_type = newline_token;

  int depth = 0;

  while(p_lex != NULL)
  {
    p_sh->type = type_of_token(p_lex);

    // if the following is a simple command
    switch (p_sh->type)
    {
      case simple_cmd_token: {
        int len = length_of_simple_command(p_lex);
        int i = 0;

        p_sh->words = (char**) malloc(sizeof(char*) * (len + 1));

        p_sh->words[len] = NULL;

        // as length goes to zero

        lex_token_t* p_lex_end;

        for (; i < len; i++)
        {
          p_sh->words[i] = p_lex->word;
          p_lex_end = p_lex;
          p_lex = p_lex->next;
        }

        p_lex = p_lex_end;

        break;
      }

      case if_token:
      case while_token:
      case until_token:
      case shell_left_token:
        depth++;
        goto default_label;


      case shell_right_token:
      case done_token:
      case fi_token:
        depth--;
        goto default_label;

      case direct_in_token:
      case direct_out_token:

        if (p_lex->next != NULL &&
            char_to_event(*(p_lex->next->word)) == regular_char)
        {
          p_lex = p_lex->next;
          p_sh->word = p_lex->word;
        }
        else
        {
          fprintf(stderr, "error: redirect without following filename\n");
          exit(1);
        }
        break;


      case newline_token:
        if (following_newline_is_ignored(prev_type))
        {
          p_lex = p_lex->next;
          continue;
        } else if (depth != 0 && following_newline_is_sequence(prev_type))
        {
          p_sh->type = sequence_token;
          p_sh->word = semicolon;
          break;
        }

      default:

default_label:
        p_sh->word = p_lex->word;
    }

#if DEBUG_SHELLER
    printf("%s, %d\n", p_sh->type == simple_cmd_token ? p_sh->words[0] : p_sh->word, depth);
#endif


    // if there are more lex tokens, make new shell token
    p_lex = p_lex->next;

    if (p_lex != NULL)
      p_sh->next = malloc(sizeof(sh_token_t));

    prev_type = p_sh->type;
    p_sh = p_sh->next;
  }

  if (depth != 0)
  {
    fprintf(stderr, "error: mismatching conditionals\n");
    exit(1);
  }

  return head_sh;
}

void test_suite(int (*get_next_byte) (void *),
      void *get_next_byte_argument);

enum
{
  SHELL_HANDLER_COUNT = 5,
};

typedef struct cmd_str_node
{
  command_t command;
  struct cmd_str_node* next;
}
cmd_node_t;

struct command_stream
{
  cmd_node_t* head;
  cmd_node_t* current;
};

command_t create_command
    (sh_token_t** token_start_ref, sh_token_t** token_end_ref);

int is_redirect_token(sh_token_t* token)
{
  return (token != NULL && (token->type == direct_in_token ||
                            token->type == direct_out_token));
}

void create_redirects(command_t c, sh_token_t ** token_ref)
{
  sh_token_t* token = *token_ref;

  if (is_redirect_token(token->next))
  {
    token = token->next;

    if (token->type == direct_in_token)
      c->input = token->word;
    else
      c->output = token->word;
  }

  if (is_redirect_token(token->next))
  {
    token = token->next;

    if (token->type == direct_in_token)
      c->input = token->word;
    else
      c->output = token->word;
  }

  *token_ref = token;
}


command_t create_if_command
   (sh_token_t **token_start_ref, sh_token_t **token_end_ref)
{
  sh_token_t *token_iter = (*token_start_ref)->next;

  sh_token_t *token_then_location = NULL;
  sh_token_t *token_else_location = NULL;
  sh_token_t *token_fi_location = NULL;

  command_t if_command = malloc(sizeof(struct command));

  command_t cond_command = NULL;
  command_t then_command = NULL;
  command_t else_command = NULL;

  if_command->type = IF_COMMAND;

  int if_depth = 0;

  for (; token_iter != *token_end_ref; token_iter = token_iter->next)
    switch (token_iter->type)
    {
      case if_token:
        if_depth++; break;
      case then_token:
        if (!if_depth) token_then_location = token_iter;
        break;
      case else_token:
        if (!if_depth) token_else_location = token_iter;
        break;
      case fi_token:
        if (!if_depth)
        {
          token_fi_location = token_iter;
          goto fi_token_found;
        }
        else if_depth--;
        break;

      default: break;
    }

fi_token_found:

  if (token_fi_location == NULL)
  {
    fprintf(stderr, "error: if statement: no matching fi\n");
    exit(1);
  }

  if (token_then_location == NULL)
  {
    fprintf(stderr, "error: if statement: no matching then\n");
    exit(1);
  }

  cond_command = create_command(&(*token_start_ref)->next, &token_then_location);

  if (token_else_location != NULL)
  {
    then_command =
      create_command(&token_then_location->next, &token_else_location);
    else_command =
      create_command(&token_else_location->next, &token_fi_location  );
  }
  else
    then_command = create_command(&token_then_location->next, &token_fi_location);
    
  if_command->u.command[0] = cond_command;
  if_command->u.command[1] = then_command;
  if_command->u.command[2] = else_command;

  create_redirects(if_command, &token_fi_location);

  *token_start_ref = token_fi_location->next;

  return if_command;
}

command_t create_while_command
   (sh_token_t **token_start_ref, sh_token_t **token_end_ref)
{
  sh_token_t *token_iter = (*token_start_ref)->next;

  sh_token_t *token_do_location = NULL;
  sh_token_t *token_done_location = NULL;

  command_t while_command = malloc(sizeof(struct command));

  command_t cond_command = NULL;
  command_t do_command = NULL;

  while_command->type = WHILE_COMMAND;

  int while_depth = 0;

  for (; token_iter != *token_end_ref; token_iter = token_iter->next)
    switch (token_iter->type)
    {
      case while_token:
      case until_token:
       while_depth++; break;
      case do_token:
        if (!while_depth) token_do_location = token_iter;
        break;
      case done_token:
        if (!while_depth)
        {
          token_done_location = token_iter;
          goto done_token_found;
        }
        else while_depth--;
        break;

      default: break;
    }

done_token_found:

  if (token_do_location == NULL)
  {
    printf("error: while statement: no matching do\n");
    exit(1);
  }

  if (token_done_location == NULL)
  {
    printf("error: while statement: no matching done\n");
    exit(1);
  }
    
  cond_command = create_command(&(*token_start_ref)->next, &token_do_location  );
    do_command = create_command( &token_do_location->next, &token_done_location);

  while_command->u.command[0] = cond_command;
  while_command->u.command[1] = do_command;

  create_redirects(while_command, &token_done_location);

  *token_start_ref = token_done_location->next;
  return while_command;
}

command_t create_until_command
   (sh_token_t **token_start_ref, sh_token_t **token_end_ref)
{
  sh_token_t *token_iter = (*token_start_ref)->next;

  sh_token_t *token_do_location = NULL;
  sh_token_t *token_done_location = NULL;

  command_t until_command = malloc(sizeof(struct command));

  command_t cond_command = NULL;
  command_t do_command = NULL;

  until_command->type = UNTIL_COMMAND;

  int until_depth = 0;

  for (; token_iter != *token_end_ref; token_iter = token_iter->next)
    switch (token_iter->type)
    {
      case while_token:
      case until_token:
       until_depth++; break;
      case do_token:
        if (!until_depth) token_do_location = token_iter;
        break;
      case done_token:
        if (!until_depth)
        {
          token_done_location = token_iter;
          goto done_token_found;
        }
        else until_depth--;
        break;

      default: break;
    }

done_token_found:

  if (token_do_location == NULL)
  {
    fprintf(stderr, "error: until statement: no matching do\n");
    exit(1);
  }

  if (token_done_location == NULL)
  {
    fprintf(stderr, "error: until statement: no matching done\n");
    exit(1);
  }
    
  cond_command = create_command(&(*token_start_ref)->next, &token_do_location  );
    do_command = create_command( &token_do_location->next, &token_done_location);

  until_command->u.command[0] = cond_command;
  until_command->u.command[1] = do_command;

  create_redirects(until_command, &token_do_location);

  *token_start_ref = token_done_location->next;
  return until_command;
}

command_t create_subshell_command
   (sh_token_t **token_start_ref, sh_token_t **token_end_ref)
{
  sh_token_t *token_iter = (*token_start_ref)->next;
  sh_token_t *token_end_location = NULL;

  command_t subshell_command = malloc(sizeof(struct command));
  subshell_command->type = SUBSHELL_COMMAND;

  int shell_depth = 0;

  for (; token_iter != *token_end_ref; token_iter = token_iter->next)
  {
    switch (token_iter->type)
    {
      case shell_left_token:
        shell_depth++;
      case shell_right_token:
        if (!shell_depth)
        {
          token_end_location = token_iter;
          goto end_token_found;
        }
        else shell_depth--;
        break;

      default: break;
    }
  }

end_token_found:

  if (token_end_location == NULL)
  {
    fprintf(stderr, "error: subshell: no matching )\n");
    exit(1);
  }
    
  command_t command =
    create_command(&(*token_start_ref)->next, &token_end_location  );

  subshell_command->u.command[0] = command;

  create_redirects(subshell_command, &token_end_location);

  *token_start_ref = token_end_location->next;

  return subshell_command;
}

command_t create_simple_command
   (sh_token_t **token_start_ref, sh_token_t **token_end_ref)
{
  NOT_USED(token_end_ref);

  command_t c = malloc(sizeof(struct command));

  c->type = SIMPLE_COMMAND;
  c->u.word = (*token_start_ref)->words;

  create_redirects(c, token_start_ref);

  *token_start_ref = (*token_start_ref)->next;

  return c;
}

/* another table of function pointers, I seem to prefer these to switch cases */
command_t (*cmd_crt_tbl[SHELL_HANDLER_COUNT])
   (sh_token_t**, sh_token_t**) = 
{
  &create_if_command,
  &create_while_command,
  &create_until_command,
  &create_simple_command,
  &create_subshell_command
};

command_t insert_top(command_t top, command_t new)
{
  command_t c_left = top;
  command_t c_right = new;

  command_t c = malloc(sizeof(struct command));

  c->u.command[0] = c_left;
  c->u.command[1] = c_right;

  return c;
}

void insert_right(command_t top, command_t new)
{
  command_t c_left = NULL;
  command_t c_right = new;
  command_t c_rightmost = top;

  while ( (c_rightmost->u.command[1]->type == PIPE_COMMAND || 
           c_rightmost->u.command[1]->type == SEQUENCE_COMMAND) )
                c_rightmost = c_rightmost->u.command[1];

  c_left = c_rightmost->u.command[1];

  c_rightmost->u.command[1] = malloc(sizeof(struct command));
  c_rightmost = c_rightmost->u.command[1];

  c_rightmost->type = PIPE_COMMAND;
  c_rightmost->u.command[0] = c_left;
  c_rightmost->u.command[1] = c_right;
}

int command_token(const sh_token_t* token)
{
  return (token->type == if_token || 
          token->type == while_token ||
          token->type == until_token ||
          token->type == shell_left_token ||
          token->type == simple_cmd_token);
}

/* externally called function, either returns command (well-formed)
 * or ends program (badly-formed) */

command_t create_command
    (sh_token_t** token_start_ref, sh_token_t** token_end_ref)
{
  command_t c = NULL;

  sh_token_t* iter_token = *token_start_ref;
  sh_token_t* end_token = *token_end_ref;

  if (iter_token == end_token)
  {
    fprintf(stderr, "error: end of command\n");
    exit(1);
  }

  while (iter_token != end_token)
  {
    // horrible nested things because there are only two operators
    
    // if this is a sequence token
    switch (iter_token->type)
    {
      case sequence_token:
      {
        if (c == NULL)
        {
          fprintf(stderr, "error: unexpected semicolon\n");
          exit(1);
        }
        else if (iter_token->next == end_token)
          break;
        else if (command_token(iter_token->next))
        {
          iter_token = iter_token->next;
          command_t new = cmd_crt_tbl[iter_token->type](&iter_token, &end_token);
          c = insert_top(c, new);
          c->type = SEQUENCE_COMMAND;
          continue;
        }
        else
        {
          fprintf(stderr, "error: unexpected token %s\n", iter_token->next->word);
          exit(1);
        }
      }
      case pipe_token:
      {
        if (c == NULL)
        {
          fprintf(stderr, "error: unexpected pipe\n");
          exit(1);
        }
        if (!command_token(iter_token->next))
        {
          fprintf(stderr, "error: invalid token after pipe\n");
          exit(1);
        }
        else
        {
          iter_token = iter_token->next;
          command_t new = cmd_crt_tbl[iter_token->type](&iter_token, &end_token);

          if (c->type == SEQUENCE_COMMAND)
            insert_right(c, new);
          else
          {
            c = insert_top(c, new);
            c->type = PIPE_COMMAND;
          }

          continue;
        }
      }

      default:
      {
        if (c != NULL)
        {
          fprintf(stderr, "error: unexpected token %s\n",
                  iter_token->type == simple_cmd_token ?
                  iter_token->words[0] : iter_token->word);
          exit(1);
        }
        if (command_token(iter_token))
        {
          c = cmd_crt_tbl[iter_token->type](&iter_token, &end_token);
          continue;
        }
        else
        {
          fprintf(stderr, "error: unexpected token %s\n", iter_token->word);
          exit(1);
        }
      }
    }

    iter_token = iter_token->next;
  }

  *token_start_ref = iter_token;

  return c;
}


/* makes empty command stream */

command_stream_t init_stream()
{

  command_stream_t stream =
    (command_stream_t) malloc(sizeof(struct command_stream));

  stream->head = malloc(sizeof(cmd_node_t));
  stream->head->command = NULL;
  stream->head->next = NULL;
  stream->current = stream->head;

  return stream;
}

sh_token_t* find_newline(sh_token_t* start_token)
{
  for (; start_token != NULL && start_token->type != newline_token;
         start_token = start_token->next)
  {
  
  }

  return start_token;
}

command_stream_t
make_command_stream (int (*get_next_byte) (void *),
		     void *get_next_byte_argument)
{
#if TEST_SUITE
  test_suite(get_next_byte, get_next_byte_argument);
#else
  // get and classify tokens
  lex_token_t* head_token_lex = 
    get_tokens(get_next_byte, get_next_byte_argument);

  __mem_store = head_token_lex->word;

  sh_token_t* head_token_sh = 
    get_shell_tokens(head_token_lex);


  // create an empty command stream
  command_stream_t stream = init_stream();

  sh_token_t *iter_token = head_token_sh;
  sh_token_t *end_token = NULL;

  while ( iter_token != NULL )
  {
    // create command, and allocate next command
    end_token = find_newline(iter_token);

    if (end_token == iter_token)
    {
      iter_token = iter_token->next;
      continue;
    }

    stream->current->command = create_command(&iter_token, &end_token);

    iter_token = end_token->next;

    if (iter_token != NULL)
    {
      stream->current->next = malloc(sizeof(cmd_node_t));
      stream->current = stream->current->next;
      stream->current->next = NULL;
    }
  }

  stream->current = stream->head;

  return stream;
#endif
  return NULL;
}

void test_suite(int (*get_next_byte) (void *),
      void *get_next_byte_argument)
{
  lex_token_t* lex_head = get_tokens(get_next_byte, get_next_byte_argument);
  lex_token_t* lex_token = lex_head;

  while (lex_token != NULL)
  {
    printf("%s ", lex_token->word);
    lex_token = lex_token->next;
  }

  sh_token_t* sh_head = get_shell_tokens(lex_head);
  sh_token_t* sh_token = sh_head;

  printf("\nPartially parsed:\n");
  
  while(sh_token != NULL)
  {
    switch (sh_token->type)
    {
      case simple_cmd_token:
        {
          char * word = sh_token->words[0];
          int i = 0;
          
          printf("((");
          for (; word != NULL; word = sh_token->words[++i])
            printf("%s ", word);
          printf("))");

          break;
        }

      case shell_left_token:
      case newline_token:
        printf("%s", sh_token->word);
        break;

      case direct_in_token:
        printf("< %s ", sh_token->word);
        break;
      case direct_out_token:
        printf("> %s ", sh_token->word);
        break;

      default:
        printf("%s ", sh_token->word);
    }
    sh_token = sh_token->next;
  }

  NOT_USED(sh_token);
}

command_t
read_command_stream (command_stream_t s)
{
  if (s->current != NULL)
  {
    cmd_node_t* tmp = s->current;
    s->current = s->current->next;
    return tmp->command;
  }

  else
  {
    free(__mem_store);
    return NULL;
  }
}

