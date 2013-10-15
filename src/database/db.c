	#include "db.h"

/* _shared_campaign_id is declared in config.h and is a global
 * readonly variable to be used for scoping purposes
*/

MYSQL * _getMySQLConnection(){
	MYSQL *conn;
	char *server = HOST;
	char *user = USERNAME;
	char *password = PASSWORD; /* set me first */
	char *database = DATABASE;
	#ifndef THREADED_DB
	/* If the connections are threaded this will be called in the main */
	mysql_library_init(0, NULL, NULL);
	#endif

	conn = mysql_init(NULL);
	return mysql_real_connect(conn, server, user, password, database, 0, NULL, 0);
}

void db_getScopeById(long id, struct gs_scope * gss, MYSQL * conn){
	MYSQL_RES * result;
	MYSQL_ROW row; 
	char query[64];

	/*Zero the scope structure */
	gs_scope_ZeroStruct(gss);

	bzero(query,sizeof query);
	sprintf(query, GS_SCOPE_GET_BY_ID, id);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}

   result = mysql_use_result(conn);
   row = mysql_fetch_row(result);
   if(row == NULL){
	  mysql_free_result(result);
	  return;    
   }

	/* Make sure id is integer */
	gs_scope_setId(atol(row[0]), gss);
	gs_scope_setDesc(row[1], gss);

	mysql_free_result(result);
}

/* We assume the calling party has used the page size to set the size of gsc */
int db_getComments(int page, long scopeId, struct gs_comment * gsc, MYSQL * conn){
	MYSQL_RES * result;
	MYSQL_ROW row; 
	int i;
	int limit;
	char query[sizeof GS_COMMENT_GET_ALL];

	bzero(query,sizeof query);
	/* In order to return the correct paginated results we have the following
	 * strategy: Retrieve the results per page, this is +1 more than we will
	 * be sending to the client. This is to make the 'nextUrl' without having
	 * to check the database for the total number of comments.
	 * To avoid losing that last telltale comment (since we discard it and it's
	 * not sent to the client) the limit offset has to be reduced by 1 if we're
	 * asking for more than one page.  
	 * For more detail and some math, see 6aa7d80
	*/
	limit = page*RESULTS_PER_PAGE;
	limit = limit > 0 ? limit-(page) : limit;
	sprintf(query, GS_COMMENT_GET_ALL, scopeId, limit);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", query);
		fprintf(stderr, "%s\n", mysql_error(conn));
		return 0;
	}

	i=0;
	result = mysql_use_result(conn);
	while( (row=mysql_fetch_row(result)) != NULL ){
		/* Initialize */
		gs_comment_ZeroStruct(&gsc[i]);

		gs_comment_setId( atol(row[0]), &gsc[i]);
		fprintf(stderr, "%s\n", row[1]);
		gs_comment_setPinId(row[1] == NULL ? -1 : atol(row[1]), &gsc[i]);
		gs_comment_setContent( row[2], &gsc[i]);
		gs_comment_setScopeId( atol(row[3]), &gsc[i]);
		gs_comment_setCreatedTime( row[4], &gsc[i]);
		gs_comment_setCommentType(row[5], &gsc[i]);
		i++;
	}
	mysql_free_result(result);  
	return i;
}

int db_getCommentsByType(int page, long scopeId, struct gs_comment * gsc, char * cType, MYSQL * conn){
	MYSQL_RES * result;
	MYSQL_ROW row; 
	int i;
	int limit;
	char query[sizeof GS_COMMENT_GET_BY_TYPE + GS_COMMENT_TYPE_LENGTH];

	bzero(query,sizeof query);
	/* For reasoning on the limit calculations:
	 * see 6aa7d80
	*/
	limit = page*RESULTS_PER_PAGE;
	limit = limit > 0 ? limit-(page) : limit;
	sprintf(query, GS_COMMENT_GET_BY_TYPE, scopeId, cType,limit);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", query);
		fprintf(stderr, "%s\n", mysql_error(conn));
		return 0;
	}

	i=0;
	result = mysql_use_result(conn);
	while( (row=mysql_fetch_row(result)) != NULL ){
		/* Initialize */
		gs_comment_ZeroStruct(&gsc[i]);

		gs_comment_setId( atol(row[0]), &gsc[i]);
		fprintf(stderr, "%s\n", row[1]);
		gs_comment_setPinId(row[1] == NULL ? -1 : atol(row[1]), &gsc[i]);
		gs_comment_setContent( row[2], &gsc[i]);
		gs_comment_setScopeId( atol(row[3]), &gsc[i]);
		gs_comment_setCreatedTime( row[4], &gsc[i]);
		gs_comment_setCommentType( row[5], &gsc[i]);
		i++;
	}
	mysql_free_result(result);  
	return i;
}

void db_getCommentById(long id, struct gs_comment * gsc, MYSQL * conn){
	MYSQL_RES * result;
	MYSQL_ROW row; 
	char query[sizeof GS_COMMENT_GET_BY_ID]; /* 72 for query, 8 for padding and null char*/

	/*Zero the scope structure */
	gs_comment_ZeroStruct(gsc);

	bzero(query,sizeof query);
	sprintf(query, GS_COMMENT_GET_BY_ID, id);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}

	result = mysql_use_result(conn);
	row = mysql_fetch_row(result);
	if(row == NULL){
		mysql_free_result(result);
		return;    
	}

	/* Make sure id is integer */
	gs_comment_setId( atol(row[0]), gsc);
	gs_comment_setPinId(row[1] == NULL ? -1 : atol(row[1]), gsc);
	gs_comment_setContent( row[2], gsc);
	gs_comment_setScopeId( atol(row[3]), gsc);
	gs_comment_setCreatedTime( row[4], gsc);
	gs_comment_setCommentType(row[5], gsc);

	mysql_free_result(result);  
}

#ifndef DB_INSERT_COMMENT_QUERY_SIZE
	#define DB_INSERT_COMMENT_QUERY_SIZE 58+140+10
#endif
void db_insertComment(struct gs_comment * gsc, MYSQL * conn){
	MYSQL_RES * result;
	MYSQL_ROW row; 
	long affected;
	char query[DB_INSERT_COMMENT_QUERY_SIZE]; /* Query, content, id, some extra padding*/


	if(gsc->scopeId == GS_SCOPE_INVALID_ID)
		return; /* Return if scope is invalid that we can tell*/

	bzero(query,sizeof query);
	sprintf(query, GS_COMMENT_INSERT, gsc->content, gsc->scopeId, gsc->pinId,gsc->cType);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}

	affected = mysql_insert_id(conn);
	if( affected == 0){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}

	/* Set the id of the comment to be what it is now  */
	gsc->id = affected;

	/* Now we could either compute the time stamp or ask the db for it. */
	bzero(query,sizeof query);
	sprintf(query,GS_COMMENT_GET_BY_ID, affected);
   
	/* Fresh Start and we want to return to the user EXACTLY what's in the db */
	gs_comment_ZeroStruct(gsc);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}

	result = mysql_use_result(conn);
	row = mysql_fetch_row(result);
	if(row == NULL){
		mysql_free_result(result);
		return;    
	}

	/* Fill er up */
	gs_comment_setId( atol(row[0]), gsc);
	gs_comment_setPinId(row[1] == NULL ? -1 : atol(row[1]), gsc);
	gs_comment_setContent( row[2], gsc);
	gs_comment_setScopeId( atol(row[3]), gsc);
	gs_comment_setCreatedTime( row[4], gsc);
	gs_comment_setCommentType( row[5], gsc);

	mysql_free_result(result);
   
}

int db_getMarkers(int page, long scopeId, struct gs_marker * gsm, MYSQL * conn){
	MYSQL_RES * result;
	MYSQL_ROW row; 
	Decimal latitude;
	Decimal longitude;
	int i;
	char query[sizeof GS_MARKER_GET_ALL];

	bzero(query,sizeof query);
	sprintf(query, GS_MARKER_GET_ALL, scopeId, page*RESULTS_PER_PAGE);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return 0;
	}

	i=0;
	result = mysql_use_result(conn);
	while( (row=mysql_fetch_row(result)) != NULL ){
		/* Initialize */
		gs_marker_ZeroStruct(&gsm[i]);

		gs_marker_setId( atol(row[0]), &gsm[i]);
		gs_marker_setCommentId( atol(row[1]), &gsm[i]);
		gs_marker_setScopeId( row[2] == NULL ?  GS_SCOPE_INVALID_ID : atol(row[2]), &gsm[i]);
		gs_marker_setCreatedTime( row[3], &gsm[i]);
		createDecimalFromString(&latitude,row[4]);
		gs_marker_setLatitude(latitude,&gsm[i]);
		createDecimalFromString(&longitude,row[5]);
		gs_marker_setLongitude(longitude,&gsm[i]);
		i++;
	}
	mysql_free_result(result);  
	return i;
}

#ifndef DB_INSERT_MARKER_QUERY_SIZE
	#define DB_INSERT_MARKER_QUERY_SIZE 84 + 32 /* 84 for Query, 32 for safety */
#endif
void db_insertMarker(struct gs_marker * gsm, MYSQL * conn){
	MYSQL_RES * result;
	MYSQL_ROW row; 
	long affected;
	char query[DB_INSERT_MARKER_QUERY_SIZE]; /* Query, content, id, some extra padding*/

	if(gsm->scopeId == GS_SCOPE_INVALID_ID)
		return; /* Return if scope is invalid that we can tell*/

	bzero(query,sizeof query);
	sprintf(query, GS_MARKER_INSERT, gsm->commentId, gsm->scopeId, gsm->latitude.left, gsm->latitude.right, gsm->longitude.left, gsm->longitude.right);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}

	affected = mysql_insert_id(conn);
	if( affected == 0){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}

	/* Set the id of the comment to be what it is now  */
	gsm->id = affected;

	/* Now we could either compute the time stamp or ask the db for it. */
	bzero(query,sizeof query);
	sprintf(query,GS_MARKER_GET_BY_ID, affected);

	/* Fresh Start and we want to return to the user EXACTLY what's in the db */
	gs_marker_ZeroStruct(gsm);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}

	result = mysql_use_result(conn);
	row = mysql_fetch_row(result);
	if(row == NULL){
		mysql_free_result(result);
		return;    
	}


	/* Fill er up */
	gs_marker_setId( atol(row[0]), gsm);
	gs_marker_setCommentId( atol(row[1]), gsm);
	gs_marker_setScopeId( row[2] == NULL ? GS_SCOPE_INVALID_ID : atol(row[2]), gsm);
	gs_marker_setCreatedTime( row[3], gsm);
	createDecimalFromString(&gsm->latitude,row[4]);
	createDecimalFromString(&gsm->longitude,row[5]);
	

	mysql_free_result(result);
   
}


void db_getMarkerById(long id, struct gs_marker * gsm, MYSQL * conn){
	MYSQL_RES * result;
	MYSQL_ROW row; 
	char query[95+5]; /* 95 for query, 5 for padding and null char*/

	/*Zero the scope structure */
	gs_marker_ZeroStruct(gsm);

	bzero(query,sizeof query);
	sprintf(query, GS_MARKER_GET_BY_ID, id);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}

	result = mysql_use_result(conn);
	row = mysql_fetch_row(result);
	if(row == NULL){
		mysql_free_result(result);
		return;    
	}

	/* Make sure id is integer */
	gs_marker_setId( atol(row[0]), gsm);
	gs_marker_setCommentId( row[1] == NULL ? GS_COMMENT_INVALID_ID :  atol(row[1]), gsm);
	gs_marker_setScopeId( row[2] == NULL ? GS_SCOPE_INVALID_ID : atol(row[2]), gsm);
	gs_marker_setCreatedTime( row[3], gsm);
	createDecimalFromString(&gsm->latitude,row[4]);
	createDecimalFromString(&gsm->longitude,row[5]);
	


	mysql_free_result(result);  
}



#ifndef DB_INSERT_HEATMAP_QUERY_SIZE
	#define DB_INSERT_HEATMAP_QUERY_SIZE 144/* 144 for safety */
#endif
void db_insertHeatmap(struct gs_heatmap * gsh, MYSQL * conn){
	MYSQL_RES * result;
	MYSQL_ROW row; 
	long affected;
	int updated;
	char query[DB_INSERT_HEATMAP_QUERY_SIZE]; /* Query, content, id, some extra padding*/

	if(gsh->scopeId == GS_SCOPE_INVALID_ID)
		return; /* Return if scope is invalid that we can tell*/

	/* Check for possible merges */
	bzero(query,sizeof query);
	sprintf(query, GS_HEATMAP_FIND_MATCH, gsh->scopeId, gsh->latitude.left, gsh->latitude.right, gsh->longitude.left, gsh->longitude.right);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}


	result = mysql_use_result(conn);
	row = mysql_fetch_row(result);
	bzero(query, sizeof query);
	updated = 0;
	if(row == NULL){
		mysql_free_result(result);
		/* This means we can insert a new one */
		sprintf(query, GS_HEATMAP_INSERT, gsh->scopeId, gsh->intensity, gsh->latitude.left, gsh->latitude.right, gsh->longitude.left, gsh->longitude.right);
	}else{
		/* Time to merge! */
		updated = 1;
		gsh->id = atol(row[0]);
		sprintf(query, GS_HEATMAP_UPDATE_BY_ID, (gsh->intensity + atol(row[1])) , atol(row[0]));	
		mysql_free_result(result);
	}


	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}

	if(updated == 0){
		/* Only if we inserted is the mysql_insert_id going to return the id 
		 * a bit of an kick is that mysql_insert_id doesn't return the id of
		 * the last UPDATED row because it assumes you already know the id.
		 * the documentation does say that it should work for update, kinda:
		 * "Returns the value generated for an AUTO_INCREMENT column by the 
		    previous INSERT or UPDATE statement. Use this function after you 
		    have performed an INSERT statement into a table that contains an 
		    AUTO_INCREMENT field, or have used INSERT or UPDATE to set a column 
		    value with LAST_INSERT_ID(expr)." -- http://dev.mysql.com/doc/refman/5.0/en/mysql-insert-id.html
		 * so our update statement apparently doesn't fit the requirement.
		*/
		affected = mysql_insert_id(conn);
		if( affected == 0){
			fprintf(stderr, "%s\n", mysql_error(conn));
			return;
		}

		/* Set the id of the comment to be what it is now  */
		gsh->id = affected;	
	}
	
	/* Now we could either compute the time stamp or ask the db for it. */
	bzero(query,sizeof query);
	sprintf(query,GS_HEATMAP_GET_BY_ID, gsh->id);

	/* Fresh Start and we want to return to the user EXACTLY what's in the db */
	gs_heatmap_ZeroStruct(gsh);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}

	result = mysql_use_result(conn);
	row = mysql_fetch_row(result);
	if(row == NULL){
		mysql_free_result(result);
		return;    
	}


	/* Fill er up */
	gs_heatmap_setId( atol(row[0]), gsh);
	gs_heatmap_setIntensity( atol(row[1]), gsh);
	gs_heatmap_setScopeId( row[2] == NULL ? GS_SCOPE_INVALID_ID : atol(row[2]), gsh);
	gs_heatmap_setCreatedTime( row[3], gsh);
	createDecimalFromString(&gsh->latitude,row[4]);
	createDecimalFromString(&gsh->longitude,row[5]);
	

	mysql_free_result(result);
   
}

#ifndef HEATMAP_PAGE_QUERY_SIZE
	#define HEATMAP_PAGE_QUERY_SIZE 512 /* Super safe estimate (could probably just be 300 or so)*/
#endif
int db_getHeatmap(int page, long scopeId, long precision, Decimal lowerLatBound, Decimal upperLatBound, Decimal lowerLonBound, Decimal upperLonBound, struct gs_heatmap * gsh, MYSQL * conn){
	MYSQL_RES * result;
	MYSQL_ROW row; 
	Decimal latitude;
	Decimal longitude;
	int i;
	char query[HEATMAP_PAGE_QUERY_SIZE];

	bzero(query,sizeof query);
	sprintf(query, 	GS_HEATMAP_GET_ALL, 
				   	precision, 	/* Latitude precision */
				   	precision, 	/* Longitude precision */
				   	scopeId,  	/* Scope */
				   	lowerLatBound.left, lowerLatBound.right , /* Latitude lower bound  */
					upperLatBound.left, lowerLatBound.right , /* Latitude upper bound  */
					lowerLonBound.left, lowerLonBound.right , /* Longitude lower bound */
					upperLonBound.left, upperLonBound.right , /* Longitude upper bound */
				   	page*HEATMAP_RESULTS_PER_PAGE);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return 0;
	}

	i=0;
	result = mysql_use_result(conn);
	while( (row=mysql_fetch_row(result)) != NULL ){
		/* Initialize */
		gs_heatmap_ZeroStruct(&gsh[i]);

		gs_heatmap_setId( page, &gsh[i]); /* Just setting the page as the id becuase these points are merged grouped heatmap points*/
		gs_heatmap_setIntensity( atol(row[0]), &gsh[i]);
		gs_heatmap_setScopeId( scopeId, &gsh[i]);
		gs_heatmap_setCreatedTime( row[1], &gsh[i]);
		createDecimalFromString(&latitude,row[2]);
		gs_heatmap_setLatitude(latitude,&gsh[i]);
		createDecimalFromString(&longitude,row[3]);
		gs_heatmap_setLongitude(longitude,&gsh[i]);
		i++;
	}
	mysql_free_result(result);  
	return i;
}

#ifndef DB_INSERT_REPORT_QUERY_SIZE
	#define DB_INSERT_REPORT_QUERY_SIZE 96 + GS_REPORT_MAX_LENGTH + (SHA_LENGTH+1)*2 +4/* 96 for Query, 65*2 for hashes, +4 for safety */
#endif
void db_insertReport(struct gs_report * gsr, MYSQL * conn){
	MYSQL_RES * result;
	MYSQL_ROW row; 
	long affected;
	char query[DB_INSERT_REPORT_QUERY_SIZE]; 

	if(gsr->scopeId == GS_SCOPE_INVALID_ID)
		return; /* Return if scope is invalid that we can tell*/

	bzero(query,sizeof query);
	sprintf(query, GS_REPORT_INSERT, gsr->content, gsr->scopeId, gsr->origin, gsr->authorize);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}

	affected = mysql_insert_id(conn);
	if( affected == 0){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}

	/* Set the id of the comment to be what it is now  */
	gsr->id = affected;

	/* Now we could either compute the time stamp or ask the db for it. */
	bzero(query,sizeof query);
	sprintf(query,GS_REPORT_GET_BY_AUTH, gsr->authorize);

	/* Fresh Start and we want to return to the user EXACTLY what's in the db */
	gs_report_ZeroStruct(gsr);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}

	result = mysql_use_result(conn);
	row = mysql_fetch_row(result);
	if(row == NULL){
		mysql_free_result(result);
		return;    
	}


	/* Fill er up */
	gs_report_setId( atol(row[0]), gsr);
	gs_report_setContent( row[1], gsr);
	gs_report_setScopeId( row[2] == NULL ? GS_SCOPE_INVALID_ID : atol(row[2]), gsr);
	strncpy(gsr->origin,row[3], SHA_LENGTH);
	strncpy(gsr->authorize, row[4], SHA_LENGTH);
	gs_report_setCreatedTime( row[5], gsr);
	

	mysql_free_result(result);
   
}

void db_getReportByAuth(char * auth, struct gs_report * gsr, MYSQL * conn){
	MYSQL_RES * result;
	MYSQL_ROW row; 
	char query[99+65+4]; /* 99 for query, 65 for auth hash, 4 for safety*/

	gs_report_ZeroStruct(gsr);
	bzero(query,sizeof query);
	
	sprintf(query, GS_REPORT_GET_BY_AUTH, auth);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return;
	}

	result = mysql_use_result(conn);
	row = mysql_fetch_row(result);
	if(row == NULL){
		mysql_free_result(result);
		return;    
	}

	gs_report_setId( atol(row[0]), gsr);
	gs_report_setContent( row[1], gsr);
	gs_report_setScopeId( row[2] == NULL ? GS_SCOPE_INVALID_ID : atol(row[2]), gsr);
	strncpy(gsr->origin,row[3], SHA_LENGTH);
	strncpy(gsr->authorize, row[4], SHA_LENGTH);
	gs_report_setCreatedTime( row[5], gsr);

	mysql_free_result(result);  
}

int db_deleteReport(struct gs_report * gsr, MYSQL * conn){
	char query[99+(64*2)+5]; /* 61 for query, 64*2+1 for hashes, 4 for safety*/

	bzero(query,sizeof query);
	sprintf(query, GS_REPORT_DELETE, gsr->origin,gsr->authorize);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return 0;
	}

	return mysql_affected_rows(conn);  
}

int db_deleteComment( long id, MYSQL * conn){
	char query[128]; /* 61 for query, 64*2+1 for hashes, 4 for safety*/

	bzero(query, sizeof query);
	sprintf(query, GS_COMMENT_DELETE, id);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return 0;
	}

	return mysql_affected_rows(conn);  
}

#ifndef REPORT_PAGE_QUERY_SIZE
	#define REPORT_PAGE_QUERY_SIZE 256 
#endif
int db_getReports(int page, long scopeId, struct gs_report * gsr, MYSQL * conn){
	MYSQL_RES * result;
	MYSQL_ROW row; 
	int i;
	char query[REPORT_PAGE_QUERY_SIZE];
	bzero(query,sizeof query);
	sprintf(query,GS_REPORT_GET_ALL,scopeId, page);

	if(0 != mysql_query(conn, query) ){
		fprintf(stderr, "%s\n", mysql_error(conn));
		return 0;
	}

	i=0;
	result = mysql_use_result(conn);
	while( (row=mysql_fetch_row(result)) != NULL ){
		/* Initialize */
		gs_report_ZeroStruct(&gsr[i]);

		gs_report_setId( atol(row[0]), &gsr[i]);
		gs_report_setContent( row[1], &gsr[i]);
		gs_report_setScopeId( row[2] == NULL ? GS_SCOPE_INVALID_ID : atol(row[2]), &gsr[i]);
		strncpy(gsr[i].origin,row[3], SHA_LENGTH);
		strncpy(gsr[i].authorize, row[4], SHA_LENGTH);
		gs_report_setCreatedTime( row[5], &gsr[i]);
		i++;
	}
	mysql_free_result(result);  
	return i;
}