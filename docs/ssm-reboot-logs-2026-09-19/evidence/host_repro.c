
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#define DAWIT_SUCCESS 0
#define DAWIT_FAIL -1
#define DAWIT_INVALID_ARGS -2
#define DAWIT_TRUE 1
#define DAWIT_LOG_ERROR(...) ((void)0)
#define DAWIT_LOG_DEBUG(...) ((void)0)
#define DAWIT_LOG_INFO(...) ((void)0)
#define DAWIT_LOG_WARNING(...) ((void)0)
#define LOG_BUFFER_CODE_SIZE 7
#define SSM_MAX_ES_LOG_READ_LINES 20
#define SSM_MAX_SYS_LOG_READ_LINES 50
#define KPI_BUFFER_SIZE 50
#define SSM_STORED_LOGS_FILE "fixture.txt"
#define OK 0
#define ERROR -1
typedef int dawit_result;
typedef FILE ssm_file;
typedef struct log_data {
    int time;
    char code[LOG_BUFFER_CODE_SIZE];
    char *message;
    int level;
    struct log_data *pre, *next;
} log_data;
typedef struct kpi_file_data {
    char data_type[8];
    char *kpi_name, *kpi_message;
} kpi_file_data;
static kpi_file_data kpi_file_table[KPI_BUFFER_SIZE];
static unsigned int kpi_file_count;
static int es_seen, sys_seen, transport_result;
static char last_message[256], last_kpi[256];
static void SSM_free_sys_kpi_table(void);
static int SSM_is_policy_done(void) { return DAWIT_TRUE; }
static int SSM_Send_ES_logs_from_file(void *p) {
    for (log_data *node = p; node; node = node->next) {
        es_seen++;
        snprintf(last_message, sizeof(last_message), "%s", node->message ? node->message : "");
    }
    return transport_result;
}
static int SSM_Send_SYS_logs_from_file(void *p, unsigned int count) {
    kpi_file_data *table = p;
    sys_seen += count;
    if (count) snprintf(last_kpi, sizeof(last_kpi), "%s", table[0].kpi_name);
    return transport_result;
}

/* Unmodified SSM_Release/DA-Service/SSM/src/SSM_file_helper.c:14-31; SHA in manifest.json. */
dawit_result SSM_file_open(ssm_file **file, char *file_name, char *open_mode)
{
	if (file_name == NULL || open_mode == NULL || file == NULL)
	{
		DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "Invalid arguments: file=[%p], file_name=[%p], open_mode=[%p]", file, file_name, open_mode);
		return DAWIT_INVALID_ARGS;
	}

	*file = fopen(file_name, open_mode);
	if (*file == NULL)
	{
		DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "Failed to open file [%s] with mode [%s], errno=%d (%s)", file_name, open_mode, errno, strerror(errno));
		return DAWIT_FAIL;
	}

	DAWIT_LOG_DEBUG(DAWIT_TAG_SSM, "Successfully opened file [%s] with mode [%s]", file_name, open_mode);
	return DAWIT_SUCCESS;
}

/* Unmodified SSM_Release/DA-Service/SSM/src/SSM_file_helper.c:34-50; SHA in manifest.json. */
dawit_result SSM_file_close(ssm_file *file)
{
	if (file == NULL)
	{
		DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "Invalid argument: file=[%p]", file);
		return DAWIT_INVALID_ARGS;
	}

	if (fclose(file) != 0)
	{
		DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "Failed to close file, errno=%d (%s)", errno, strerror(errno));
		return DAWIT_FAIL;
	}

	DAWIT_LOG_DEBUG(DAWIT_TAG_SSM, "Successfully closed file");
	return DAWIT_SUCCESS;
}

/* Unmodified SSM_Release/DA-Service/SSM/src/SSM_file_helper.c:53-79; SHA in manifest.json. */
dawit_result SSM_file_read(ssm_file *file, unsigned char *buffer, unsigned int read_size)
{
	int read_count = 0;

	if (file == NULL || buffer == NULL)
	{
		DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "Invalid arguments: file=[%p], buffer=[%p]", file, buffer);
		return DAWIT_INVALID_ARGS;
	}

	read_count = fread(buffer, 1, read_size, file);

	if (read_count != read_size)
	{
		if (feof(file))
		{
			DAWIT_LOG_DEBUG(DAWIT_TAG_SSM, "Reached end of file");
		}
		else
		{
			DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "Failed to read file: expected [%u] bytes, read [%d] bytes, errno=%d (%s)", read_size, read_count, errno, strerror(errno));
		}
		return DAWIT_FAIL;
	}

	return DAWIT_SUCCESS;
}

/* Unmodified SSM_Release/DA-Service/SSM/src/SSM_file_helper.c:103-118; SHA in manifest.json. */
dawit_result SSM_file_seek(ssm_file *file, long offset, int whence)
{
	if (file == NULL)
	{
		DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "Invalid argument: file=[%p]", file);
		return DAWIT_INVALID_ARGS;
	}

	if (fseek(file, offset, whence) != 0)
	{
		DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "Failed to seek file: offset=[%ld], whence=[%d], errno=%d (%s)", offset, whence, errno, strerror(errno));
		return DAWIT_FAIL;
	}

	return DAWIT_SUCCESS;
}

/* Unmodified SSM_Release/DA-Service/SSM/src/SSM_file_logger.c:423-819; SHA in manifest.json. */
/* Skip characters until newline is found */
static void SSM_skip_to_next_line(ssm_file *file)
{
	unsigned char ch;
	while (SSM_file_read(file, &ch, 1) == DAWIT_SUCCESS)
	{
		if (ch == '\n')
		{
			break;
		}
	}
}

/* Read one ES log record from file directly into a log_data node.
 * Reads character by character, no intermediate line buffer needed.
 * Returns log_data node on success, NULL if non-ES/invalid/EOF.
 * *is_eof is set to true when end of file is reached. */
static log_data* SSM_read_ES_log_from_file(ssm_file *file, bool *is_eof)
{
	unsigned char ch;
	int field = 0;  /* 0=type, 1=time, 2=code, 3=message */
	int type_value = 0;
	int time_value = 0;
	char code_value[LOG_BUFFER_CODE_SIZE] = {0};
	int code_pos = 0;
	char *message_buf = NULL;
	int message_pos = 0;
	int message_size = 0;
	dawit_result read_result = DAWIT_SUCCESS;

	if (is_eof != NULL)
	{
		*is_eof = false;
	}

	/* Look for start of record '<' */
	ch = '\0';
	while ((read_result = SSM_file_read(file, &ch, 1)) == DAWIT_SUCCESS)
	{
		if (ch == '<')
		{
			break;
		}
		/* Skip anything before '<' (including newlines) */
	}

	/* If we couldn't read, it's EOF */
	if (ch != '<')
	{
		if (is_eof != NULL) *is_eof = true;
		DAWIT_LOG_DEBUG(DAWIT_TAG_SSM, "SSM ES file read EOF");
		return NULL;
	}

	/* Parse fields character by character */
	while ((read_result = SSM_file_read(file, &ch, 1)) == DAWIT_SUCCESS)
	{
		if (ch == '>')
		{
			/* End of record - consume trailing newline if present */
			unsigned char next_ch;
			if (SSM_file_read(file, &next_ch, 1) == DAWIT_SUCCESS && next_ch != '\n')
			{
				/* Not a newline, continue reading */
				SSM_file_seek(file, -1, SEEK_CUR); /* Put the character back */
			}

			/* Null terminate message */
			if (message_buf != NULL)
			{
				message_buf[message_pos] = '\0';
			}

			/* Only create node for ES records (type 0) with valid code */
			if (type_value != 0 || code_pos == 0)
			{
				free(message_buf);
				if (is_eof != NULL) *is_eof = true;
				DAWIT_LOG_DEBUG(DAWIT_TAG_SSM, "SSM ES file read EOF");
				return NULL;
			}

			log_data *new_node = (log_data *)calloc(1, sizeof(log_data));
			if (new_node == NULL)
			{
				DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "Failed to allocate memory for log_data node");
				free(message_buf);
				return NULL;
			}

			new_node->time = time_value;
			snprintf(new_node->code, LOG_BUFFER_CODE_SIZE, "%s", code_value);
			new_node->message = message_buf;
			new_node->level = 5;
			new_node->pre = NULL;
			new_node->next = NULL;
			return new_node;
		}

		if (ch == '#')
		{
			field++;
			continue;
		}

		switch (field)
		{
			case 0: /* Type */
				if (ch >= '0' && ch <= '9')
				{
					type_value = type_value * 10 + (ch - '0');
				}
				break;
			case 1: /* Time */
				if (ch >= '0' && ch <= '9')
				{
					time_value = time_value * 10 + (ch - '0');
				}
				break;
			case 2: /* Code */
				if (code_pos < LOG_BUFFER_CODE_SIZE - 1)
				{
					code_value[code_pos++] = ch;
				}
				break;
			case 3: /* Message - dynamically grow buffer */
				if (message_pos >= message_size - 1)
				{
					message_size += 64;
					char *new_buf = (char *)realloc(message_buf, message_size);
					if (new_buf == NULL)
					{
						DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "Failed to allocate memory for message");
						free(message_buf);
						SSM_skip_to_next_line(file);
						return NULL;
					}
					message_buf = new_buf;
				}
				message_buf[message_pos++] = ch;
				break;
		}
	}

	/* EOF reached before completing record */
	if (is_eof != NULL) *is_eof = true;
	free(message_buf);
	return NULL;
}

/* Append a log_data node to the doubly linked list */
static void SSM_append_log_to_list(log_data **head, log_data **tail, log_data *new_node)
{
	if (new_node == NULL)
	{
		return;
	}

	if (*head == NULL)
	{
		*head = new_node;
		*tail = new_node;
	}
	else
	{
		(*tail)->next = new_node;
		new_node->pre = *tail;
		*tail = new_node;
	}
}

/* Read up to SSM_MAX_ES_LOG_READ_LINES ES log records from an already-open file.
 * Returns linked list of log_data. Sets *is_eof to true when file ends. */
static log_data* SSM_read_ES_logs_batch(ssm_file *file, bool *is_eof)
{
	log_data *head = NULL;
	log_data *tail = NULL;
	int es_count = 0;

	/* Read records directly into linked list, up to SSM_MAX_ES_LOG_READ_LINES ES records */
	while (es_count < SSM_MAX_ES_LOG_READ_LINES && !(*is_eof))
	{
		log_data *new_node = SSM_read_ES_log_from_file(file, is_eof);
		if (new_node != NULL)
		{
			SSM_append_log_to_list(&head, &tail, new_node);
			es_count++;
		}
		/* NULL means non-ES record, invalid line, or EOF - continue until es_count reached or EOF */
	}

	DAWIT_LOG_INFO(DAWIT_TAG_SSM, "Read batch of %d ES log records", es_count);
	return head;
}

/* Read one SYS log record from file and store in file data table.
 * Returns 1 on success (SYS record saved), 0 if non-SYS/invalid record, -1 on EOF. */
static int SSM_read_and_restore_SYS_log(ssm_file *file, bool *is_eof)
{
    unsigned char ch;
    int field = 0;  /* 0=type, 1=kpi_name, 2=data_type, 3=message */
    int type_value = 0;
    char *kpi_name_buf = NULL;
    int kpi_name_pos = 0;
    int kpi_name_size = 0;
    char data_type_buf[8] = {0};
    int data_type_pos = 0;
    char *message_buf = NULL;
    int message_pos = 0;
    int message_size = 0;

    if (is_eof != NULL) *is_eof = false;

    /* Look for start of record '<' */
    ch = '\0';
    while (SSM_file_read(file, &ch, 1) == DAWIT_SUCCESS)
    {
        if (ch == '<') break;
    }

    if (ch != '<')
    {
        if (is_eof != NULL) *is_eof = true;
        return -1;
    }

    /* Parse fields character by character */
    while (SSM_file_read(file, &ch, 1) == DAWIT_SUCCESS)
    {
        if (ch == '>')
        {
            /* Consume trailing newline if present */
            unsigned char next_ch;
            if (SSM_file_read(file, &next_ch, 1) == DAWIT_SUCCESS && next_ch != '\n')
            {
                /* Not a newline, continue reading */
                SSM_file_seek(file, -1, SEEK_CUR); /* Put the character back */
            }

            /* Null terminate buffers */
            if (kpi_name_buf != NULL) kpi_name_buf[kpi_name_pos] = '\0';
            if (message_buf != NULL) message_buf[message_pos] = '\0';

            /* Only process SYS records (type 1) with valid kpi_name */
            if (type_value != 1 || kpi_name_pos == 0)
            {
                free(kpi_name_buf);
                free(message_buf);
                return 0;
            }

            /* Check if we have space in the file data table */
            if (kpi_file_count >= KPI_BUFFER_SIZE) {
                DAWIT_LOG_WARNING(DAWIT_TAG_SSM, "KPI file data table is full, discarding entry");
                free(kpi_name_buf);
                free(message_buf);
                return 0;
            }

            /* Store data in file data table */
            kpi_file_table[kpi_file_count].kpi_name = kpi_name_buf;
            snprintf(kpi_file_table[kpi_file_count].data_type, 
                sizeof(kpi_file_table[kpi_file_count].data_type), "%s", data_type_buf);
            kpi_file_table[kpi_file_count].kpi_message = message_buf;

            DAWIT_LOG_INFO(DAWIT_TAG_SSM, "Stored in file data table: name=%s, type=%s, message=%s",
                kpi_file_table[kpi_file_count].kpi_name,
                kpi_file_table[kpi_file_count].data_type,
                kpi_file_table[kpi_file_count].kpi_message ? 
                kpi_file_table[kpi_file_count].kpi_message : "");

            kpi_file_count++;
            return 1;
        }

        if (ch == '#')
        {
            field++;
            continue;
        }

        switch (field)
        {
            case 0: /* Type */
                if (ch >= '0' && ch <= '9') type_value = type_value * 10 + (ch - '0');
                break;
            case 1: /* KPI name */
                if (kpi_name_pos >= kpi_name_size - 1)
                {
                    kpi_name_size += 64;
                    char *new_buf = (char *)realloc(kpi_name_buf, kpi_name_size);
                    if (new_buf == NULL)
                    {
                        DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "Failed to allocate memory for KPI name");
                        free(kpi_name_buf);
                        free(message_buf);
                        SSM_skip_to_next_line(file);
                        return 0;
                    }
                    kpi_name_buf = new_buf;
                }
                kpi_name_buf[kpi_name_pos++] = ch;
                break;
            case 2: /* Data type string */
                if (data_type_pos < (int)sizeof(data_type_buf) - 1) data_type_buf[data_type_pos++] = ch;
                break;
            case 3: /* Message */
                if (message_pos >= message_size - 1)
                {
                    message_size += 64;
                    char *new_buf = (char *)realloc(message_buf, message_size);
                    if (new_buf == NULL)
                    {
                        DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "Failed to allocate memory for message");
                        free(kpi_name_buf);
                        free(message_buf);
                        SSM_skip_to_next_line(file);
                        return 0;
                    }
                    message_buf = new_buf;
                }
                message_buf[message_pos++] = ch;
                break;
        }
    }

    /* EOF reached before completing record */
    if (is_eof != NULL) *is_eof = true;
    free(kpi_name_buf);
    free(message_buf);
    return -1;
}


/* Read up to SSM_MAX_SYS_LOG_READ_LINES SYS log records from file and
 * directly save them to KPI_buffer_table_array (no linked list needed).
 * Returns the number of SYS records restored in this batch. */
static int SSM_restore_SYS_logs_batch(ssm_file *file, bool *is_eof)
{
	int sys_count = 0;

	while (sys_count < SSM_MAX_SYS_LOG_READ_LINES && !(*is_eof))
	{
		int result = SSM_read_and_restore_SYS_log(file, is_eof);
		if (result == 1)
		{
			sys_count++;
		}
		/* result == 0: non-SYS record or invalid, continue */
		/* result == -1: EOF, loop will exit */
	}

	DAWIT_LOG_INFO(DAWIT_TAG_SSM, "Restored batch of %d SYS log records to KPI buffer", sys_count);
	return sys_count;
}

/* Restore ES log records from file-read linked list into log_buffer,
 * then call SSM_Send_ES_log_protected() which reads from log_buffer to send. */
static dawit_result SSM_restore_and_send_ES_logs(log_data *head)
{
	if (head == NULL)
	{
		DAWIT_LOG_INFO(DAWIT_TAG_SSM, "No ES logs to restore");
		return DAWIT_FAIL;
	}

	dawit_result result = SSM_Send_ES_logs_from_file((void *)head);
	if (result == DAWIT_SUCCESS)
	{
		DAWIT_LOG_INFO(DAWIT_TAG_SSM, "ES logs sent to server successfully");
	}
	else
	{
		DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "Failed to send ES logs to server");
	}

	return result;
}


/* Free the linked list of log_data */
static void SSM_free_log_data_list(log_data *head)
{
	log_data *current = head;
	log_data *next_node = NULL;

	while (current != NULL)
	{
		next_node = current->next;
		if (current->message != NULL)
		{
			free(current->message);
		}
		free(current);
		current = next_node;
	}
}

/* Unmodified SSM_Release/DA-Service/SSM/src/SSM_file_logger.c:831-948; SHA in manifest.json. */
dawit_result SSM_read_logs_from_file_and_send_impl(void)
{
	ssm_file *file = NULL;
	bool is_eof = false;
	int es_batch_num = 0;
	int sys_batch_num = 0;
	int total_es_count = 0;
	int total_sys_count = 0;
	dawit_result result = DAWIT_SUCCESS;
	
    if (SSM_is_policy_done() != DAWIT_TRUE)
	{
		DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "SSM policy is not yet loaded");
		return DAWIT_FAIL;
	}
	/* Open file for reading */
	result = SSM_file_open(&file, SSM_STORED_LOGS_FILE, "r");
	if (result != DAWIT_SUCCESS)
	{
		DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "Failed to open %s file for reading", SSM_STORED_LOGS_FILE);
		return DAWIT_FAIL;
	}

	/* Read ES records in batches of 20, print each batch, until file ends */
	DAWIT_LOG_INFO(DAWIT_TAG_SSM, "=== Reading ES Records ===");
	while (!is_eof)
	{
		log_data *log_list = SSM_read_ES_logs_batch(file, &is_eof);

		if (log_list == NULL)
		{
			break;
		}

		es_batch_num++;
		DAWIT_LOG_INFO(DAWIT_TAG_SSM, "=== ES Batch [%d] ===", es_batch_num);

		log_data *current = log_list;
		while (current != NULL)
		{
			total_es_count++;
			current = current->next;
		}

		/* Restore ES logs to buffer and send to server */
		SSM_restore_and_send_ES_logs(log_list);

		SSM_free_log_data_list(log_list);
	}

	/* SYS records are at end of file, continue reading from current position */
	is_eof = false;

	/* Read SYS records in batches of 50 and save directly to KPI_buffer_table_array */
	DAWIT_LOG_INFO(DAWIT_TAG_SSM, "=== Reading SYS Records ===");
	while (!is_eof)
	{
		int batch_count = SSM_restore_SYS_logs_batch(file, &is_eof);

		if (batch_count == 0)
		{
			break;
		}

		sys_batch_num++;
		DAWIT_LOG_INFO(DAWIT_TAG_SSM, "=== SYS Batch [%d] ===", sys_batch_num);

		total_sys_count += batch_count;
	}

	SSM_file_close(file);

	/* Send all SYS logs from KPI buffer to server (after all batches are restored) */
	if (total_sys_count > 0)
	{

		dawit_result sys_result = SSM_Send_SYS_logs_from_file(kpi_file_table, kpi_file_count);
		if (sys_result == DAWIT_SUCCESS)
		{
			DAWIT_LOG_INFO(DAWIT_TAG_SSM, "SYS logs sent to server successfully");
		}
		else
		{
			DAWIT_LOG_ERROR(DAWIT_TAG_SSM, "Failed to send SYS logs to server");
		}

		SSM_free_sys_kpi_table();
	}

	DAWIT_LOG_INFO(DAWIT_TAG_SSM, "Total [%d] ES logs in [%d] batches, [%d] SYS logs in [%d] batches",
		total_es_count, es_batch_num, total_sys_count, sys_batch_num);

	if (total_es_count == 0 && total_sys_count == 0)
	{
		DAWIT_LOG_INFO(DAWIT_TAG_SSM, "No logs found in file");
		return DAWIT_FAIL;
	}

	return DAWIT_SUCCESS;
}

static void SSM_free_sys_kpi_table(void)
{
	for (unsigned int i = 0; i < kpi_file_count && i < KPI_BUFFER_SIZE; i++)
    {
        if (kpi_file_table[i].kpi_name != NULL)
        {
            free(kpi_file_table[i].kpi_name);
            kpi_file_table[i].kpi_name = NULL;
        }
        if (kpi_file_table[i].kpi_message != NULL)
        {
            free(kpi_file_table[i].kpi_message);
            kpi_file_table[i].kpi_message = NULL;
        }
    }
    kpi_file_count = 0;
}

/* Unmodified TR_Utils/apps/examples/crashrpt_comprehensive_test/crashrpt_comprehensive_test_main.c:281-347; SHA in manifest.json. */
static int verify_report(const char *filepath)
{
	FILE *fp;
	char line[256];
	int found_header = 0;
	int tc_found = 0;
	int expected_found = 0;
	int actual_found = 0;
	int report_seq = 0;

	printf("\n");
	printf("===========================================\n");
	printf("Verifying: %s\n", filepath);
	printf("===========================================\n");

	fp = fopen(filepath, "r");
	if (fp == NULL) {
		printf("  ERROR: Cannot open file, errno %d\n", errno);
		return ERROR;
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		if (strstr(line, "CRASH REPORT") != NULL) {
			found_header = 1;
			printf("  Header: FOUND\n");
		} else if (sscanf(line, "Test Case: %d", &tc_found) == 1) {
			printf("  Test Case: %d\n", tc_found);
		} else if (sscanf(line, "Expected Reason: %d", &expected_found) == 1) {
			printf("  Expected Reason: %d\n", expected_found);
		} else if (sscanf(line, "Actual Reason: %d", &actual_found) == 1) {
			printf("  Actual Reason: %d\n", actual_found);
		} else if (sscanf(line, "Report Sequence: %d", &report_seq) == 1) {
			printf("  Report Sequence: %d\n", report_seq);
		}
	}

	fclose(fp);

	printf("===========================================\n");

	if (!found_header) {
		printf("VERIFICATION: FAILED (no header found)\n");
		return ERROR;
	}

	/* Check reason code match */
	const char *reason_str;
	switch (actual_found) {
		case 1: reason_str = "UNKNOWN"; break;
		case 2: reason_str = "APP_ASSERT"; break;
		case 3: reason_str = "KERNEL_ASSERT"; break;
		case 4: reason_str = "HW_FAULT"; break;
		default: reason_str = "INVALID"; break;
	}

	printf("Reason code %d = %s\n", actual_found, reason_str);

	/* Verify reason code matches expectation */
	if (actual_found == expected_found) {
		printf("VERIFICATION: PASSED (reason code matches)\n");
		return OK;
	} else {
		printf("VERIFICATION: PARTIAL (reason code mismatch: expected %d, got %d)\n",
			   expected_found, actual_found);
		return OK; /* Still consider it a pass since crash was captured */
	}
}

static void fixture(const char *path, const char *text) {
    FILE *fp = fopen(path, "w"); assert(fp);
    assert(fputs(text, fp) >= 0); assert(fclose(fp) == 0);
}
static int run_case(const char *name, const char *input, int transport) {
    es_seen = sys_seen = 0; last_message[0] = last_kpi[0] = 0;
    transport_result = transport;
    fixture(SSM_STORED_LOGS_FILE, input);
    int result = SSM_read_logs_from_file_and_send_impl();
    printf("CASE %s: result=%d es=%d sys=%d first_sys=%s last_message=%s\n",
           name, result, es_seen, sys_seen, last_kpi, last_message);
    return result;
}
int main(void) {
    int result;
    result = run_case("mixed_two_SYS", "<0#1609459200#CS001#hello>\n<1#first#NUM#10>\n<1#second#NUM#20>\n", DAWIT_SUCCESS);
    assert(result == DAWIT_SUCCESS && es_seen == 1 && sys_seen == 1 && strcmp(last_kpi, "second") == 0);
    result = run_case("single_SYS", "<1#only#NUM#10>\n", DAWIT_SUCCESS);
    assert(result == DAWIT_FAIL && sys_seen == 0);
    result = run_case("transport_failure", "<0#1609459200#CS001#hello>\n<1#first#NUM#10>\n<1#second#NUM#20>\n", DAWIT_FAIL);
    assert(result == DAWIT_SUCCESS && es_seen == 1 && sys_seen == 1);
    run_case("hash_message", "<0#1609459200#CS001#before#after>\n", DAWIT_SUCCESS);
    assert(strcmp(last_message, "before") == 0);
    run_case("angle_message", "<0#1609459200#CS001#before>after>\n", DAWIT_SUCCESS);
    assert(strcmp(last_message, "before") == 0);
    fixture("wrong_reason.txt", "CRASH REPORT\nTest Case: 1\nExpected Reason: 2\nActual Reason: 3\nReport Sequence: 0\n");
    result = verify_report("wrong_reason.txt");
    printf("CASE wrong_reason: verify_result=%d (OK=%d)\n", result, OK);
    assert(result == OK);
    fixture("header_only.txt", "CRASH REPORT\n");
    result = verify_report("header_only.txt");
    printf("CASE header_only: verify_result=%d (OK=%d)\n", result, OK);
    assert(result == OK);
    puts("REPRODUCED: all seven observed defects/scenarios; these assertions characterize current bugs, not desired behavior.");
    return 0;
}
