///////////////////////////////////////////////////////////////////////////////
//
// @@@ START COPYRIGHT @@@
//
// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// @@@ END COPYRIGHT @@@
//
///////////////////////////////////////////////////////////////////////////////

using namespace std;

#include <errno.h>
#include <assert.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <iostream>
#include <mpi.h>
#include "msgdef.h"
#include "seabed/trace.h"
#include "montrace.h"
#include "monlogging.h"
#include "pnodeconfig.h"


///////////////////////////////////////////////////////////////////////////////
//  Physical Node Configuration
///////////////////////////////////////////////////////////////////////////////

CPNodeConfig::CPNodeConfig( CPNodeConfigContainer *pnodesConfig
                          , int                    pnid
                          , int                    excludedLastCore
                          , int                    excludedFirstCore
                          , const char            *hostname
                          )
            : pnodesConfig_(pnodesConfig)
            , pnid_(pnid)
            , excludedFirstCore_(excludedFirstCore)
            , excludedLastCore_(excludedLastCore)
            , spareNode_(false)
            , sparePNids_(NULL)
            , sparePNidsCount_(0)
            , next_(NULL)
            , prev_(NULL)
{
    const char method_name[] = "CPNodeConfig::CPNodeConfig";
    TRACE_ENTRY;

    int len = strlen( hostname );
    assert( len <= MPI_MAX_PROCESSOR_NAME );

    strcpy( name_, hostname );

    CPU_ZERO( &excludedCoreMask_ );

    TRACE_EXIT;
}

CPNodeConfig::~CPNodeConfig( void )
{
    const char method_name[] = "CPNodeConfig::~CPNodeConfig";
    TRACE_ENTRY;

    if (sparePNids_)
    {
        delete [] sparePNids_;
    }

    TRACE_EXIT;
}

int CPNodeConfig::GetSpareList( int sparePNids[] )
{
    const char method_name[] = "CPNodeConfig::GetSpareList";
    TRACE_ENTRY;
    
    if ( ! sparePNids_ ||  ! sparePNids ) 
    {
        return(0);
    }
    
    if ( sparePNidsCount_ > 0 )
    {
        for ( int i = 0; i < sparePNidsCount_ ; i++ )
        {
            sparePNids[i] = sparePNids_[i];
        }
    }
    
    TRACE_EXIT;
    return(sparePNidsCount_);
}

void CPNodeConfig::ResetSpare( void )
{
    const char method_name[] = "CPNodeConfig::ResetSpare";
    TRACE_ENTRY;
    
    spareNode_ = false;
    sparePNidsCount_ = 0;
    if (sparePNids_)
    {
        delete [] sparePNids_;
    }
    
    TRACE_EXIT;
}

void CPNodeConfig::SetSpareList( int sparePNids[], int spareCount )
{
    const char method_name[] = "CPNodeConfig::SetSpareList";
    TRACE_ENTRY;
    
    assert( ! sparePNids_ || spareCount );

    sparePNidsCount_ = spareCount;
    sparePNids_ = new int [sparePNidsCount_];

    if ( ! sparePNids_ )
    {
        int err = errno;
        char la_buf[MON_STRING_BUF_SIZE];
        sprintf(la_buf, "[%s], Error: Can't allocate spare pnids array - errno=%d (%s)\n", method_name, err, strerror(errno));
        mon_log_write(MON_PNODECONF_SET_SPARE_1, SQ_LOG_CRIT, la_buf);
    }
    else
    {
        for ( int i = 0; i < sparePNidsCount_ ; i++ )
        {
            sparePNids_[i] = sparePNids[i];
            if (trace_settings & TRACE_INIT)
            {
                trace_printf("%s@%d - Added spare pnid=%d to spare node array in (pnid=%d, nodename=%s)\n", method_name, __LINE__, sparePNids_[i], pnid_, name_);
            }
        }
        spareNode_ = true;
    }

    TRACE_EXIT;
}

void CPNodeConfig::SetName( const char *newName ) 
{ 
    if (newName) 
    {
        strcpy(name_, newName); 
    }
} 

void CPNodeConfig::SetExcludedFirstCore( int excludedFirstCore ) 
{ 
    excludedFirstCore_ = excludedFirstCore;
} 

void CPNodeConfig::SetExcludedLastCore( int excludedLastCore ) 
{ 
    excludedLastCore_ = excludedLastCore;
} 

CPNodeConfigContainer::CPNodeConfigContainer( int pnodesConfigMax )
                     : pnodeConfig_(NULL)
                     , pnodesCount_(0)
                     , snodesCount_(0)
                     , nextPNid_(0)
                     , pnodesConfigMax_(pnodesConfigMax)
                     , head_(NULL)
                     , tail_(NULL)
{
    const char method_name[] = "CPNodeConfigContainer::CPNodeConfigContainer";
    TRACE_ENTRY;

    pnodeConfig_ = new CPNodeConfig *[pnodesConfigMax_];

    if ( ! pnodeConfig_ )
    {
        int err = errno;
        char la_buf[MON_STRING_BUF_SIZE];
        sprintf(la_buf, "[%s], Error: Can't allocate physical node configuration array - errno=%d (%s)\n", method_name, err, strerror(errno));
        mon_log_write(MON_PNODECONF_CONSTR_1, SQ_LOG_CRIT, la_buf);
    }
    else
    {
        // Initialize array
        if ( pnodeConfig_ )
        {
            for ( int i = 0; i < pnodesConfigMax_; i++ )
            {
                pnodeConfig_[i] = NULL;
            }
        }
    }

    TRACE_EXIT;
}

CPNodeConfigContainer::~CPNodeConfigContainer( void )
{
    CPNodeConfig *pnodeConfig = head_;

    const char method_name[] = "CPNodeConfigContainer::~CPNodeConfigContainer";
    TRACE_ENTRY;

    // Delete entries
    while ( head_ )
    {
        DeletePNodeConfig( pnodeConfig );
        pnodeConfig = head_;
    }

    // Initialize array
    if ( pnodeConfig_ )
    {
        for ( int i = 0; i < pnodesConfigMax_ ;i++ )
        {
            pnodeConfig_[i] = NULL;
        }
    }

    TRACE_EXIT;
}

void CPNodeConfigContainer::Clear( void )
{
    const char method_name[] = "CPNodeConfigContainer::Clear";
    TRACE_ENTRY;

    CPNodeConfig *pnodeConfig = head_;

    // Delete entries
    while ( head_ )
    {
        DeletePNodeConfig( pnodeConfig );
        pnodeConfig = head_;
    }

    if ( pnodeConfig_ )
    {
        // Initialize array
        if ( pnodeConfig_ )
        {
            for ( int i = 0; i < pnodesConfigMax_ ;i++ )
            {
                pnodeConfig_[i] = NULL;
            }
        }
    }

    pnodesCount_ = 0;
    snodesCount_ = 0;
    nextPNid_ = 0;
    head_ = NULL;
    tail_ = NULL;

    TRACE_EXIT;
}

CPNodeConfig *CPNodeConfigContainer::AddPNodeConfig( int   pnid
                                                   , char *name
                                                   , int   excludedFirstCore
                                                   , int   excludedLastCore
                                                   , bool  spare
                                                   )
{
    const char method_name[] = "CPNodeConfigContainer::AddPNodeConfig";
    TRACE_ENTRY;

    // pnid list is NOT sequential from zero
    if ( ! (pnid >= 0 && pnid < pnodesConfigMax_) )
    {
        char la_buf[MON_STRING_BUF_SIZE];
        sprintf(la_buf, "[%s], Error: Invalid pnid=%d - should be >= 0 and < %d)\n", method_name, pnid, pnodesConfigMax_);
        mon_log_write(MON_PNODECONF_ADD_PNODE_1, SQ_LOG_CRIT, la_buf);
        return( NULL );
    }

    assert( pnodeConfig_[pnid] == NULL );

    CPNodeConfig *pnodeConfig = new CPNodeConfig( this
                                                , pnid
                                                , excludedFirstCore
                                                , excludedLastCore
                                                , name );
    if (pnodeConfig)
    {
        if ( spare )
        {
            snodesCount_++;
            spareNodesConfigList_.push_back( pnodeConfig );
        }

        // Bump the physical node count
        pnodesCount_++;
        // Add it to the array
        pnodeConfig_[pnid] = pnodeConfig;
        // Add it to the container list
        if ( head_ == NULL )
        {
            head_ = tail_ = pnodeConfig;
        }
        else
        {
            tail_->next_ = pnodeConfig;
            pnodeConfig->prev_ = tail_;
            tail_ = pnodeConfig;
        }

        // Set the next available pnid
        nextPNid_ = (pnid == nextPNid_) ? (pnid+1) : nextPNid_ ;
        if ( nextPNid_ == pnodesConfigMax_ )
        {   // We are at the limit, search for unused pnid from begining
            nextPNid_ = -1;
            for (int i = 0; i < pnodesConfigMax_; i++ )
            {
                if ( pnodeConfig_[i] == NULL )
                {
                    nextPNid_ = i;
                    break;
                }
            }
        }
        else if ( pnodeConfig_[nextPNid_] != NULL )
        {   // pnid is in use
            int next = ((nextPNid_ + 1) < pnodesConfigMax_) ? nextPNid_ + 1 : 0 ;
            nextPNid_ = -1;
            for (int i = next; i < pnodesConfigMax_; i++ )
            {
                if ( pnodeConfig_[i] == NULL )
                {
                    nextPNid_ = i;
                    break;
                }
            }
        }

        if (trace_settings & (TRACE_INIT | TRACE_REQUEST))
        {
            trace_printf( "%s@%d - Added physical node configuration object\n"
                          "        (pnid=%d, nextPNid_=%d)\n"
                          "        (pnodesCount_=%d,pnodesConfigMax=%d)\n"
                        , method_name, __LINE__
                        , pnid, nextPNid_
                        , pnodesCount_, pnodesConfigMax_);
        }
    }
    else
    {
        int err = errno;
        char la_buf[MON_STRING_BUF_SIZE];
        sprintf(la_buf, "[%s], Error: Can't allocate physical node configuration object - errno=%d (%s)\n", method_name, err, strerror(errno));
        mon_log_write(MON_PNODECONF_ADD_PNODE_2, SQ_LOG_ERR, la_buf);
    }

    TRACE_EXIT;
    return( pnodeConfig );
}

void CPNodeConfigContainer::DeletePNodeConfig( CPNodeConfig *pnodeConfig )
{
    const char method_name[] = "CPNodeConfigContainer::DeletePNodeConfig";
    TRACE_ENTRY;

    if (trace_settings & (TRACE_INIT | TRACE_REQUEST))
    {
        trace_printf( "%s@%d Deleting node=%s, pnid=%d, nextPNid_=%d\n"
                     , method_name, __LINE__
                     , pnodeConfig->GetName()
                     , pnodeConfig->GetPNid()
                     , nextPNid_ );
    }

    int pnid = pnodeConfig->GetPNid();
    pnodeConfig_[pnid] = NULL;

    if ( head_ == pnodeConfig )
        head_ = pnodeConfig->next_;
    if ( tail_ == pnodeConfig )
        tail_ = pnodeConfig->prev_;
    if ( pnodeConfig->prev_ )
        pnodeConfig->prev_->next_ = pnodeConfig->next_;
    if ( pnodeConfig->next_ )
        pnodeConfig->next_->prev_ = pnodeConfig->prev_;
    delete pnodeConfig;

    // Decrement the physical node configuration count
    pnodesCount_--;

    if ( nextPNid_ == -1 )
    { // We are at the limit, use the deleted pnid as the next available
        nextPNid_ = pnid;
    }
    else if ( nextPNid_ > pnid )
    { // Always use the lower pnid value
        nextPNid_ = pnid;
    }

    if (trace_settings & (TRACE_INIT | TRACE_REQUEST))
    {
        trace_printf( "%s@%d - Deleted physical node configuration object\n"
                      "        (pnid=%d, nextPNid_=%d)\n"
                      "        (pnodesCount_=%d,pnodesConfigMax=%d)\n"
                    , method_name, __LINE__
                    , pnid, nextPNid_
                    , pnodesCount_, pnodesConfigMax_);
    }
    TRACE_EXIT;
}

int CPNodeConfigContainer::GetPNid( char *nodename )
{
    const char method_name[] = "CPNodeConfigContainer::GetPNid";
    TRACE_ENTRY;

    int pnid = -1;
    CPNodeConfig *config = head_;
    while (config)
    {
        if ( strcmp( config->GetName(), nodename ) == 0 )
        { 
            pnid = config->GetPNid();
            break;
        }
        config = config->GetNext();
    }
    
    TRACE_EXIT;
    return( pnid );
}

CPNodeConfig *CPNodeConfigContainer::GetPNodeConfig( char *nodename )
{
    const char method_name[] = "CPNodeConfigContainer::GetPNodeConfig";
    TRACE_ENTRY;

    CPNodeConfig *config = head_;
    while (config)
    {
        if ( strcmp( config->GetName(), nodename ) == 0 )
        { 
            break;
        }
        config = config->GetNext();
    }

    TRACE_EXIT;
    return config;
}

CPNodeConfig *CPNodeConfigContainer::GetPNodeConfig( int pnid )
{
    const char method_name[] = "CPNodeConfigContainer::GetPNodeConfig";
    TRACE_ENTRY;

    CPNodeConfig *config = head_;
    while (config)
    {
        if ( config->GetPNid() == pnid )
        { 
            break;
        }
        config = config->GetNext();
    }

    TRACE_EXIT;
    return config;
}

void CPNodeConfigContainer::GetSpareNodesConfigSet( const char *name
                                                  , PNodesConfigList_t &spareSet )
{
    bool foundInSpareSet = false;
    CPNodeConfig *spareNodeConfig;
    CPNodeConfig *pNodeconfig;
    PNodesConfigList_t tempSpareSet;

    const char method_name[] = "CPNodeConfigContainer::GetSpareNodesConfigSet";
    TRACE_ENTRY;

    PNodesConfigList_t::iterator itSn;
    for ( itSn = spareNodesConfigList_.begin(); 
          itSn != spareNodesConfigList_.end(); 
          itSn++ ) 
    {
        spareNodeConfig = *itSn;
        if (trace_settings & TRACE_INIT)
        {
            trace_printf( "%s@%d - %s is a configured spare node\n"
                        , method_name, __LINE__
                        , spareNodeConfig->GetName());
        }

        // Build the set of pnids that constitute the 'spare set'
        int sparePNidsCount = spareNodeConfig->GetSparesCount()+1;
        int *sparePNids = new int [sparePNidsCount];
        spareNodeConfig->GetSpareList( sparePNids );
        sparePNids[spareNodeConfig->GetSparesCount()] = spareNodeConfig->GetPNid();

        // Build the list of configured physical nodes that
        // constitute the 'spare set'
        for ( int i = 0; i < sparePNidsCount ; i++ )
        {
            pNodeconfig = GetPNodeConfig( sparePNids[i] );
            if ( pNodeconfig )
            {
                tempSpareSet.push_back( pNodeconfig );
                if (trace_settings & TRACE_INIT)
                {
                    trace_printf( "%s@%d - Added %s as member of spare set (%s, count=%ld)\n"
                                , method_name, __LINE__
                                , pNodeconfig->GetName()
                                , spareNodeConfig->GetName()
                                , tempSpareSet.size() );
                }
            }
        }
        
        // Check each node in the 'spare set'
        PNodesConfigList_t::iterator itSnSet;
        for ( itSnSet = tempSpareSet.begin(); 
              itSnSet != tempSpareSet.end(); 
              itSnSet++ ) 
        {
            pNodeconfig = *itSnSet;
            if (trace_settings & TRACE_INIT)
            {
                trace_printf( "%s@%d - %s is in spare set (%s, count=%ld)\n"
                            , method_name, __LINE__
                            , pNodeconfig->GetName()
                            , spareNodeConfig->GetName()
                            , tempSpareSet.size() );
            }
            if ( strcmp( pNodeconfig->GetName(), name ) == 0 )
            {
                foundInSpareSet = true;
                spareSet = tempSpareSet;
                if (trace_settings & TRACE_INIT)
                {
                    trace_printf( "%s@%d - Found %s in spare set (%s, count=%ld)\n"
                                , method_name, __LINE__
                                , pNodeconfig->GetName()
                                , spareNodeConfig->GetName()
                                , tempSpareSet.size() );
                }
            }
        }
        if (sparePNids)
        {
            tempSpareSet.clear();
            delete [] sparePNids;
        }
        if (foundInSpareSet)
        {
            break;
        }
    }

    TRACE_EXIT;
}

