/* testjson.c */

#include <stdio.h>

#include "mongo.h"
#include "json/json.h"
#include "md5.h"

char * json_to_bson( char * js ){
    struct json_object * o = json_tokener_parse(js);
    struct bson_buffer bb;
    
    if ( is_error( o ) )
        return 0;
    
    if ( ! json_object_is_type( o , json_type_object ) ){
        fprintf( stderr , "json_to_bson needs a JSON object, not type\n" );
        return 0;
    }
    
    bson_buffer_init( &bb );
    json_object_object_foreach(o,k,v){
        if ( v ){
            switch ( json_object_get_type( v ) ){
            case json_type_int:
                bson_append_int( &bb , k , json_object_get_int( v ) );
                break;
            case json_type_boolean:
                bson_append_bool( &bb , k , json_object_get_boolean( v ) );
                break;
            case json_type_double:
                bson_append_double( &bb , k , json_object_get_double( v ) );
                break;
            case json_type_string:
                bson_append_string( &bb , k , json_object_get_string( v ) );
                break;
            default:
                fprintf( stderr , "can't handle type for : %s\n" , json_object_to_json_string(v) );
                return 0;
            }
        }
        else {
            bson_append_null( &bb , k );
        }
    }
    return bson_finish( &bb );
}

int json_to_bson_test( char * js , int size , const char * hash ){
    struct bson b;
    md5_state_t st;
    md5_byte_t digest[16];
    char myhash[33];
    int i;

    fprintf( stderr , "----\n%s\n" , js );

    
    bson_init( &b , json_to_bson( js ) , 1 );

    if ( b.data == 0 ){
        if ( size == 0 )
            return 1;
        fprintf( stderr , "error: %s\n" , js );
        return 0;
        
    }
    
    if ( size != bson_size( &b ) ){
        fprintf( stderr , "sizes don't match [%s] want != got %d != %d\n" , js , size , bson_size(&b) );
        bson_destory( &b );
        return 0;
    }    

    bson_print( &b );
    

    md5_init(&st);
    md5_append( &st , (const md5_byte_t*)b.data , bson_size( &b ) );
    md5_finish(&st, digest);

    for ( i=0; i<16; i++ )
        sprintf( myhash + ( i * 2 ) , "%.2x" , digest[i] );
    myhash[32] = 0;

    if ( strlen( hash ) != 32 ){
        fprintf( stderr , "\tinvalid hash given got %s\n" , myhash );
        bson_destory( &b );
        return 0;
    }
    else if ( strstr( myhash , hash ) != myhash ){
        fprintf( stderr , "\t hashes don't match\n\t%s\n\t%s\n" , myhash , hash );
        bson_destory( &b );
        return 0;
    }
    
    bson_destory( &b );
    return 1;
}

int total = 0;
int fails = 0;

int run_json_to_bson_test( char * js , int size , const char * hash ){
    total++;
    if ( ! json_to_bson_test( js , size , hash ) )
        fails++;
    
    return fails;
}

#define JSONBSONTEST run_json_to_bson_test

int main(){

    run_json_to_bson_test( "1" , 0 , 0 );
    
    JSONBSONTEST( "{ 'x' : true }" , 9 , "6fe24623e4efc5cf07f027f9c66b5456" );
    JSONBSONTEST( "{ 'x' : null }" , 8 , "12d43430ff6729af501faf0638e68888" );
    JSONBSONTEST( "{ 'x' : 5.2 }" , 16 , "aaeeac4a58e9c30eec6b0b0319d0dff2" );
    JSONBSONTEST( "{ 'x' : 'eliot' }" , 18 , "331a3b8b7cbbe0706c80acdb45d4ebbe" );
    JSONBSONTEST( "{ 'x' : 5.2 , 'y' : 'truth' , 'z' : 1.1 }" , 40 , "7c77b3a6e63e2f988ede92624409da58" );
    /*
    JSONBSONTEST( "{ 'x' : 5.2 , 'y' : { 'a' : 'eliot' , b : true } , 'z' : null }" , 44 , "b3de8a0739ab329e7aea138d87235205" );
    JSONBSONTEST( "{ 'x' : 5.2 , 'y' : [ 'a' , 'eliot' , 'b' , true ] , 'z' : null }" , 62 , "cb7bad5697714ba0cbf51d113b6a0ee8" );
    */
    JSONBSONTEST( "{ 'x' : 4 }" , 12 , "d1ed8dbf79b78fa215e2ded74548d89d" );
    JSONBSONTEST( "{ 'x' : 5.2 , 'y' : 'truth' , 'z' : 1 }" , 36 , "8993953de080e9d4ef449d18211ef88a" );
    JSONBSONTEST( "{ 'x' : 'eliot' , 'y' : true , 'z' : 1 }" , 29 , "24e79c12e6c746966b123310cb1a3290" );

    printf( "----\ntotal: %d\nfails : %d\n" , total , fails );
    
    return fails;
}
