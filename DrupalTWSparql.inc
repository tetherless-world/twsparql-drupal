<?php

/*
 * To change this template, choose Tools | Templates
 * and open the template in the editor.
 */

include_once('common/TWSparql.inc');

/**
 * Defines a logger object for TW Sparql module to provide feedback to
 * users specific to Drupal.
 * @author pattoe 
 * @author pwest
 */
class DrupalTWSparqlLogger implements TWSparqlLogger {
  public static function init() {
    TWSparql::$logger = new DrupalTWSparqlLogger();
  }
  
  /**
   * Return whether debugging is turned on or not
   */
  public function shouldDebug() {
    return variable_get( 'twsparql_debug', FALSE ) ;
  }

  /**
   * Set whether or not we should debug
   */
  public function enableDebug($val) {
    variable_set('twsparql_debug',$val);
  }

  /**
   * Logs a string as an error.
   * @param string $str String to log to the error console. 
   */
  public function logError($str) {
	if( $this->shouldDebug() ) {
	  drupal_set_message( $str, 'error' ) ;
	}
  }
  
  /**
   * Logs a string as a warning
   * @param string $str String to log to the warning console. 
   */
  public function logWarning($str) {
	if( $this->shouldDebug() ) {
	  drupal_set_message( $str, 'warning' ) ;
	}
  }
  
  /**
   * Logs a string as a status
   * @param string $str String to log to the status console. 
   */
  public function logStatus($str) {
	if( $this->shouldDebug() ) {
	  drupal_set_message( $str, 'status' ) ;
	}
  }
}

/**
 * Defines Drupal specific cache object for the TWSparql module to cache intermediate and final results of a query.
 * @author pattoe
 * @author pwest
 */
class DrupalTWSparqlCache implements TWSparqlCache {
  public static function init() {
    TWSparql::$cache = new DrupalTWSparqlCache();
  }
  
  /**
   * Caches a query result document for a particular URI
   * @param string $queryUri A URI that 'executes' the query file to get
   * the query text
   * @param string $results The query text returned by dereferencing $queryUri 
   */
  public function cacheQueryResult($pageid, $queryUri, $endpoint, $results)
  {
    // clear anything in the database already for these params
    $this->clearQueryResultCache( $pageid, $queryUri, $endpoint ) ;

    // add the new data to the database
    $sql = "INSERT INTO {twsparql_query_result_cache} (nid, queryuri, endpoint, value) VALUES(%d, \"%s\", \"%s\", \"%s\")";
    $res = db_query( $sql, $pageid, $queryUri, $endpoint, $results ) ;
  }
  
  /**
   * Caches the final transformation of a query using an XSL template.
   * @param string $queryUri The URI used to generate the SPARQL results
   * @param string $xsltUri The URI used to dereference the XSL stylesheet
   * @param string $result The final result applying the transformation named by
   * $xsltUri to the document returned by deferencing $queryUri.
   * @param string $expires An optional duration to wait before expiring the
   * result; defaults to TWSparql::getEngine()->getCacheTTL()
   */
  public function cacheFinal($pageid, $queryUri, $endpoint, $xsltUri, $result)
  {
    // clear anything in the database already for these params
    $this->clearFinalCache( $pageid, $queryUri, $endpoint, $xsltUri ) ;

    // add the new data to the database
    $sql = "INSERT INTO {twsparql_final_cache} (nid, queryuri, endpoint, xslturi, value) VALUES(%d, \"%s\", \"%s\", \"%s\", \"%s\")";
    TWSparql::getLogger()->logStatus( "cacheFinal sql = $sql" ) ;
    $res = db_query( $sql, $pageid, $queryUri, $endpoint, $xsltUri, $result ) ;
  }
  
  /**
   * Clears the cached copy of a query result
   *
   * if any of the parameters are specified then clear just the row(s) that
   * contain those values.
   *
   * if none of the parameters are specified then clear  all rows
   *
   * @param string $pageid 
   * @param string $queryUri 
   * @param string $endpoint 
   */
  public function clearQueryResultCache($pageid, $queryUri, $endpoint)
  {
    if( empty( $pageid ) && empty( $queryUri ) && !empty( $endpoint ) )
    {
      db_query("DELETE FROM {twsparql_query_result_cache}");
    }
    else
    {
      $sql = "DELETE FROM {twsparql_query_result_cache} where nid=\"$pageid\" AND queryuri=\"$queryUri\" AND endpoint=\"$endpoint\"" ;
      TWSparql::getLogger()->logStatus( "clearQueryResultCache sql = $sql");
      db_query( $sql ) ;
    }
  }

  public function clearFinalCache($pageid, $queryUri, $endpoint, $xsltUri)
  {
    if( empty( $pageid ) && empty( $queryUri ) &&
        empty( $endpoint ) && empty( $xsltUri ) )
    {
      db_query("DELETE FROM {twsparql_final_cache}");
    }
    else
    {
      $sql = "DELETE FROM {twsparql_final_cache} where nid=\"$pageid\" AND queryuri=\"$queryUri\" AND endpoint=\"$endpoint\" AND xslturi=\"$xsltUri\"" ;
      TWSparql::getLogger()->logStatus( "clearFinalCache sql = $sql" ) ;
      db_query( $sql ) ;
    }
  }
  
  /**
   * See if the date sent plus the expires value is less than now
   *
   * @param string $cachedon when the value was cached
   * @param string $expires length of time the cached value is valid
   * @return boolean true if expired false otherwise
   */
  private function isexpired( $cachedon, $expires )
  {
    if( $expires == 0 || $expires == "0" )
    {
        return true ;
    }

    $breakdown = explode( " ", $expires ) ;
    $num = $breakdown[0] ;
    $unit = $breakdown[1] ;
    switch( $unit )
    {
        case "H":
            $use_unit = "hour" ;
            break ;
        case "D":
            $use_unit = "day" ;
            break ;
        case "M":
            $use_unit = "month" ;
            break ;
    }

    $expire_date = new DateTime( $cachedon ) ;
    if( !$expire_date )
    {
        return true ;
    }

    $expire_date->modify( "+$num $use_unit" ) ;
    $now = new DateTime() ;

    $result = true ;
    if( $now <= $expire_date ) $result = false ;
    return $result ;
  }

  /**
   * Gets the cached content for $queryUri, or NULL if no content is available.
   * @param string $queryUri 
   */
  public function getCachedQueryResult($pageid, $queryUri, $endpoint, $expires)
  {
    $res = NULL ;
    // see if the pageid,quri,ep is in the DB where the cachedon date
    // plus expires is less then NOW. If it is, return the results
    $sql = "SELECT cachedon, value FROM {twsparql_query_result_cache} WHERE nid = $pageid AND queryuri = \"$queryUri\" AND endpoint = \"$endpoint\"";
    $results = db_query( $sql ) ;
    if( $results != NULL )
    {
        $data = db_fetch_array( $results ) ;
        if( $data != NULL )
        {
            $cachedon = $data['cachedon'] ;
            $value = $data['value'] ;
            if( $this->isexpired( $cachedon, $expires ) == false )
            {
                TWSparql::getLogger()->logStatus( "getCachedQueryResult Is Cached" ) ;
                $res = $value ;
            }
            else
            {
                TWSparql::getLogger()->logStatus( "getCachedQueryResult Cache Expired" ) ;
            }
        }
        else
        {
            TWSparql::getLogger()->logStatus( "getCachedQueryResult no data returned" ) ;
        }
    }
    else
    {
        TWSparql::getLogger()->logStatus( "getCachedQueryResult no results returned" ) ;
    }

    return $res ;
  }

  public function getCachedFinal($pageid, $queryUri, $endpoint, $xsltUri,
                                 $expires)
  {
    $res = NULL ;
    // see if the pageid,quri,ep is in the DB where the cachedon date
    // plus expires is less then NOW. If it is, return the results
    $sql = "SELECT cachedon, value FROM {twsparql_final_cache} WHERE nid = %d AND queryuri = \"%s\" AND endpoint = \"%s\" AND xslturi = \"%s\"";
    $results = db_query( $sql, $pageid, $queryUri, $endpoint, $xsltUri ) ;
    if( $results != NULL )
    {
        $data = db_fetch_array( $results ) ;
        if( $data != NULL )
        {
            $cachedon = $data['cachedon'] ;
            $value = $data['value'] ;
            if( $this->isexpired( $cachedon, $expires ) == false )
            {
                TWSparql::getLogger()->logStatus( "getCachedFinal Is Cached" ) ;
                $res = $value ;
            }
            else
            {
                TWSparql::getLogger()->logStatus( "getCachedFinal Cached Expired" ) ;
            }
        }
        else
        {
            TWSparql::getLogger()->logStatus( "getCachedFinal no data returned" ) ;
        }
    }
    else
    {
        TWSparql::getLogger()->logStatus( "getCachedFinal no results returned" ) ;
    }

    return $res ;
  }
}

class DrupalTWSparql extends TWSparql {
  public static function init() {
    TWSparql::$engine = new DrupalTWSparql();
  }
  
  public function shouldDebug() {
	return TWSparql::getLogger()->shouldDebug() ;
  }
  
  public function enableDebug($val) {
	TWSparql::getLogger()->enableDebug($val) ;
  }
  
  public function shouldEnablePHPExtensions() {
    return variable_get('twsparql_phpext',FALSE);
  }
  
  public function enablePHPExtensions($val) {
    variable_set('twsparql_phpext',$val);
  }
  
  public function getSiteBase() {
	return base_path() ;
  }

  public function getIbase() {
    return variable_get('twsparql_ibase',"http://example.com/instances/");
  }
  
  public function setIbase($val) {
    variable_set('twsparql_ibase',$val);
  }
  
  public function getSbase() {
    return variable_get('twsparql_sbase',"http://example.com/schema/");
  }
  
  public function setSbase($val) {
    variable_set('twsparql_sbase',$val);
  }
  
  public function getXsltPath() {
    return variable_get('twsparql_xsltpath','http://example.com/xsl/');
  }
  
  public function setXsltPath($val) {
    variable_set('twsparql_xsltpath',$val);
  }
  
  public function getQueryPath() {
    return variable_get('twsparql_querypath','http://example.com/query/');
  }
  
  public function setQueryPath($val) {
    variable_set('twsparql_querypath',$val);
  }
  
  public function getDefaultXsl() {
    return variable_get('twsparql_xslt','http://example.com/default.xsl');
  }
  
  public function setDefaultXsl($val) {
    variable_set('twsparql_xslt',$val);
  }
  
  public function getEndpoint() {
    return variable_get('twsparql_endpoint','http://example.com/sparql');
  }
  
  public function setEndpoint($val) {
    variable_set('twsparql_endpoint',$val);
  }
  
  public function getCacheTTL() {
    return variable_get('twsparql_cache_expires','1 D');
  }
  
  public function setCacheTTL($val) {
    variable_set('twsparql_cache_expires',$val);
  }
  
  public function getCacheEnabled() {
    return variable_get('twsparql_cache_enabled',TRUE);
  }
  
  public function setCacheEnabled($val) {
    variable_set('twsparql_cache_enabled',$val);
  }
}

// Could have just used the DrupalTWSparql static function init to
// initialize all three of these, but could be the case where someone
// wants to use different loggers and caching mechanisms instead of the
// Drupal one here.
DrupalTWSparql::init();
DrupalTWSparqlLogger::init();
DrupalTWSparqlCache::init();

