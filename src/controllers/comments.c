#include "controllers/comments.h"

static inline int min(const int a, const int b){
	return a < b ? a : b;
}

/*Internal Routing for the comment controller.
 *Responsible to delegate to GET,POST,PUT,DELETE methods here.
 *Will return whatever the result of the request is into the 
 *stringToReturn. Will NOT overflow the buffer. If not enough space
 *is given controller may return an invalid response.
 *Returns the status code of the response.
*/
int comment_controller(const struct http_request * request, char * stringToReturn, int strLength){
	char buffer[(RESULTS_PER_PAGE * sizeof(struct gs_comment))*4+1+(2*MAX_URL_LENGTH)];
	int status;
	
	status = 503;
	bzero(buffer, sizeof buffer);

	switch(request->method){
		case GET:
			if( (status = comments_get(buffer, sizeof buffer, request )) == -1 )
				goto cc_nomem;
			/* Process results of comments_get */
			snprintf(stringToReturn,strLength,"%s",buffer);
			break;
		case POST:
			status = comment_post(buffer,sizeof buffer,request);
			if( status == -1 )
				goto cc_nomem;
			snprintf(stringToReturn,strLength,"%s",buffer);
			break;
		case DELETE:
			status = comment_delete(buffer, sizeof buffer, request);
			snprintf(stringToReturn,strLength,"%s",buffer);
			break;
		default:
			/*Invalid Method Err*/
			status = 501;	
			goto cc_unsupportedMethod;
			break;
	}
	

	return status;

	cc_nomem: /*Comment Controller Memory Allocation fail */
		snprintf(stringToReturn, strLength, ERROR_STR_FORMAT, 500, NOMEM_ERROR);
		return status;

	cc_unsupportedMethod:/*Comment Controller Bad method*/			
		snprintf(stringToReturn, strLength, ERROR_STR_FORMAT, status, BAD_METHOD_ERR);
		return status;															

}

int comments_get(char * buffer, int buffSize ,const struct http_request * request){
	struct gs_comment * commentPage;
	int numComments;
	MYSQL *conn;
	int page;
	int nextPage;
	char nextStr[MAX_URL_LENGTH];
	char prevStr[MAX_URL_LENGTH];
	char json[COMMENT_JSON_LENGTH];
	char tempBuf[20];
	char cType[COMMENTS_CTYPE_SIZE];
	char commentBuffer[buffSize];
	int i, numParams;
	StrMap * sm;


	page=1;
	bzero(tempBuf, sizeof tempBuf);
	bzero(cType, sizeof cType);

	
	commentPage = malloc(RESULTS_PER_PAGE * sizeof(struct gs_comment));
	if(commentPage == NULL){
		return -1; 
	}

	sm = sm_new(HASH_TABLE_CAPACITY);
	if(sm == NULL){
		free(commentPage);
		return -1;
	}

	/* Parse the URL */
	numParams = parseURL(request->url, strlen(request->url), sm);
	if(numParams > 0){
		if(sm_exists(sm, "page") == 1)
			if(sm_get(sm, "page", tempBuf, sizeof tempBuf) == 1){
				page = atoi(tempBuf); /* TODO: use strtol */
				if(page <= 0){
					/* Err */		
					sm_delete(sm);
					free(commentPage);
					snprintf(buffer, buffSize, ERROR_STR_FORMAT, 422, BAD_PAGE_ERR);
					return 422;
				}

			}
		if(sm_exists(sm, "type") ==1){
			if(sm_get(sm,"type", cType, sizeof cType) == 1){
				/* Verify that it is a correct type */
				if(strncasecmp(cType, CTYPE_1,COMMENTS_CTYPE_SIZE) != 0)
					if(strncasecmp(cType, CTYPE_2,COMMENTS_CTYPE_SIZE) != 0)
						if(strncasecmp(cType, CTYPE_3,COMMENTS_CTYPE_SIZE) != 0){
							sm_delete(sm);
							free(commentPage);
							snprintf(buffer, buffSize, ERROR_STR_FORMAT, 422, BAD_TYPE_ERR);
							return 422;
						}

			}
		}else{
			/*No Type, set flag*/
			cType[0] = '\0';
		}
	}
	sm_delete(sm);

	
	/* For the client we start numbering from 1, for internal use we need to use
	 * page-1 because the page is part of the calculation of the limit term in
	 * the query. So to get the first page it needs to be 0.
	*/
	page -=1;

	mysql_thread_init();
	conn = _getMySQLConnection();
	if(!conn){
		free(commentPage);
		mysql_thread_end();
		fprintf(stderr, "%s\n", "Could not connect to mySQL on worker thread");
		return -1;
	}

	bzero(nextStr, sizeof nextStr);
	bzero(prevStr, sizeof prevStr);
	bzero(commentBuffer,buffSize);

	if(cType[0] == '\0')
		numComments = db_getComments(page, _shared_campaign_id ,commentPage, conn);
	else
		numComments = db_getCommentsByType(page, _shared_campaign_id, commentPage, cType, conn);

	mysql_close(conn);
	mysql_thread_end();

	if( numComments > RESULTS_RETURNED ){
		nextPage = page+1;
		if(cType[0] == '\0')
			snprintf(nextStr,MAX_URL_LENGTH, "%scomments?page=%d", BASE_API_URL, nextPage);
		else
			snprintf(nextStr,MAX_URL_LENGTH, "%scomments?page=%d&type=%s", BASE_API_URL, nextPage, cType);
	} else {
		snprintf(nextStr,MAX_URL_LENGTH, "null");
	}

	if(page > 1)
		if(cType[0] == '\0')
			snprintf(prevStr,MAX_URL_LENGTH,"%scomments?page=%d",BASE_API_URL,page-1);
		else
			snprintf(prevStr,MAX_URL_LENGTH,"%scomments?page=%d&type=%s",BASE_API_URL,page-1,cType);
	else
		snprintf(prevStr,MAX_URL_LENGTH,"null");

	/* Build the actual list of JSON objects for the comments
	 * use the buffer itself as our store and hope for the best.
	 * note the use of min here is neccesary. We grab RESULTS_RETURNED+1 
	 * from the database to check the next page. So we need that -1 or
	 * the number we've actually been returned
	*/
	for(i=0; i < min(numComments,RESULTS_RETURNED); ++i){
		bzero(json,sizeof json);
		gs_commentToNJSON(commentPage[i],json,sizeof json);
		/* This could be done more efficiently
		 * by performing the string cpy manually and saving the place of 
		 * the end of the string to continue writing
		*/
		if(i==0)
			snprintf(commentBuffer,buffSize,"%s",json);
		else{
			strncat(commentBuffer,",",buffSize);
			strncat(commentBuffer,json,buffSize);
		}			
	}

	free(commentPage);
	snprintf(buffer,buffSize, COMMENT_PAGE_STR, 200, commentBuffer, min(numComments,RESULTS_RETURNED), page+1, nextStr,prevStr);
	
	
	return 200;
}

int comment_post(char * buffer, int buffSize, const struct http_request * request){
	MYSQL *conn;
	struct gs_comment insComment;
	StrMap * sm;
	int i;
	int empty;
	char keyBuffer[GS_COMMENT_MAX_LENGTH+5]; /* Add +5 so we can check if the length is beyond its limit */
	char valBuffer[GS_COMMENT_MAX_LENGTH+1];
	char **convertSuccess;

	bzero(keyBuffer,sizeof keyBuffer);
	gs_comment_ZeroStruct(&insComment);
	convertSuccess = NULL;

	sm = sm_new(HASH_TABLE_CAPACITY);
	if(sm == NULL){
		fprintf(stderr, "sm err\n");
		return -1;
	}

	/*Parse the JSON for the information we desire */
	parseJSON(request->data,request->contentLength, sm);

	/* Determine if the request is valid or not */
	if(sm_exists(sm, "type") !=1 || sm_exists(sm, "message") !=1){
		sm_delete(sm);
		fprintf(stderr, "required keys not found\n"); /* TODO replace with network logging */
		snprintf(buffer, buffSize, ERROR_STR_FORMAT, 400, MISSING_KEY_ERR);
		return 400;		
	}else{
		if(sm_exists(sm,"message") == 1){
			if(sm_get(sm,"message",keyBuffer, sizeof keyBuffer) == 1){
				if(strlen(keyBuffer) > GS_COMMENT_MAX_LENGTH){
					sm_delete(sm);
					snprintf(buffer, buffSize, ERROR_STR_FORMAT, 422, "Message may not be more than " STRINGIFY(GS_COMMENT_MAX_LENGTH)  " characters long." );
					return 422;
				}
			}
		}
		if(sm_exists(sm, "type") ==1){
			if(sm_get(sm,"type", valBuffer, sizeof valBuffer) == 1){
				/* Verify that it is a correct type */
				if(strncasecmp(valBuffer, CTYPE_1,COMMENTS_CTYPE_SIZE) != 0)
					if(strncasecmp(valBuffer, CTYPE_2,COMMENTS_CTYPE_SIZE) != 0)
						if(strncasecmp(valBuffer, CTYPE_3,COMMENTS_CTYPE_SIZE) != 0){				
							sm_delete(sm);
							snprintf(buffer, buffSize, ERROR_STR_FORMAT, 422, BAD_TYPE_ERR);
							return 422;
						}

			}
		}
	}

	/* valid, cary on and copy over */
	bzero(keyBuffer, sizeof keyBuffer);
	gs_comment_setScopeId(_shared_campaign_id, &insComment);
	sm_get(sm, "message",keyBuffer, sizeof keyBuffer);
	/* Check for message being empty */
	empty = 1;
	for(i=0; i < (int)strlen(keyBuffer); ++i){
		 empty = empty && (keyBuffer[i] == ' ');
	}
	if(empty){
		sm_delete(sm);
		snprintf(buffer, buffSize, ERROR_STR_FORMAT, 422, EMPTY_COMMENT_MESSAGE);
		return 422;
	}
	
	gs_comment_setContent(keyBuffer, &insComment);
	if(sm_exists(sm, "pin")){
		sm_get(sm, "pin",keyBuffer,sizeof keyBuffer);
		if(strtod(keyBuffer,convertSuccess) != 0 && convertSuccess == NULL){
			gs_comment_setPinId(atol(keyBuffer),&insComment);
		}else{
			/* NaN */
			sm_delete(sm);
			snprintf(buffer, buffSize, ERROR_STR_FORMAT, 422, MISSING_PIN_ERR);
			return 422;
		} 
	}
	sm_get(sm, "type", insComment.cType, sizeof insComment.cType);
	sm_delete(sm);



	mysql_thread_init();
	conn = _getMySQLConnection();
	if(!conn){
		mysql_thread_end();
		fprintf(stderr, "%s\n", "Could not connect to mySQL on worker thread");
		return -1;
	}
	db_insertComment(&insComment, conn);
	mysql_close(conn);
	mysql_thread_end();

	/* populate the response */
	snprintf(buffer,buffSize,"{\"status_code\" : 200,\"message\" : \"Succesfully submited new comment\"}");

	return 200;
}


int comment_delete(char * buffer, int buffSize, const struct http_request * request){
	StrMap * sm;
	MYSQL *conn;
	long affected; 
	long id;

	sm = sm_new(HASH_TABLE_CAPACITY);
	if(sm == NULL){
		fprintf(stderr, "sm err\n");
		return -1;
	}
	parseURL(request->url, strlen(request->url), sm);
	if(sm_exists(sm,"id")!=1){
		sm_delete(sm);
		snprintf(buffer, buffSize, ERROR_STR_FORMAT, 400, MISSING_ID_KEY);
		return 400;
	}
	sm_get(sm, "id", buffer, sizeof buffer);
	id = atol(buffer);
	sm_delete(sm);

	mysql_thread_init();
	conn = _getMySQLConnection();
	if(!conn){
		mysql_thread_end();
		fprintf(stderr, "%s\n", "Could not connect to mySQL on worker thread");
		return 500;
	}	

	affected = db_deleteComment(id,conn);

	mysql_close(conn);
	mysql_thread_end();

	if(affected > 0){
		snprintf(buffer,buffSize,"{\"status_code\" : 204,\"message\" : \"Successfuly deleted comment\"}");
		return 204;
	} else {
		snprintf(buffer,buffSize,"{\"status_code\" : 404,\"message\" : \"Could not find comment with given id\"}");
		return 404;
	}
}