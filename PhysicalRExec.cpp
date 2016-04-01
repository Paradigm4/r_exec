/*
**
* BEGIN_COPYRIGHT
*
* PARADIGM4 INC.
* This file is part of the Paradigm4 Enterprise SciDB distribution kit
* and may only be used with a valid Paradigm4 contract and in accord
* with the terms and conditions specified by that contract.
*
* Copyright © 2010 - 2012 Paradigm4 Inc.
* All Rights Reserved.
*
* END_COPYRIGHT
*/

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "query/Operator.h"

#define MAIN         // we are the main program, we need to define this
#define SOCK_ERRORS  // we will use verbose socket errors

#include "sisocks.h"
#include "Rconnection.h"
#include "RExecSettings.h"
#include <log4cxx/logger.h>

namespace scidb
{
	
using namespace std; // -> SciDB 15.7

// Logger for operator. static to prevent visibility of variable outside of file
static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("scidb.qproc.rexec"));

class PhysicalRExec : public PhysicalOperator
{
public:
    PhysicalRExec(string const& logicalName,
                           string const& physicalName,
                           Parameters const& parameters,
                           ArrayDesc const& schema):
        PhysicalOperator(logicalName, physicalName, parameters, schema)
    {}

    class OutputWriter
    {
    private:
        std::shared_ptr<Array> _output;
        Coordinates _outputChunkPosition;
        Coordinates _outputCellPosition;
        size_t _nAttrs;
        vector< std::shared_ptr<ArrayIterator> > _outputArrayIterators;
        vector< std::shared_ptr<ChunkIterator> > _outputChunkIterators;
        Value _dval;

    public:
        OutputWriter(ArrayDesc const& schema, std::shared_ptr<Query>& query):
            _output(new MemArray(schema, query)),
            _outputChunkPosition(2, -1),
            _outputCellPosition(2, 0),
            _nAttrs(schema.getAttributes(true).size()),
            _outputArrayIterators(_nAttrs),
            _outputChunkIterators(_nAttrs)
        {
            _outputChunkPosition[0] = query->getInstanceID();
            _outputCellPosition[0] = query->getInstanceID();
            for (size_t i =0; i<_nAttrs; ++i)
            {
                _outputArrayIterators[i] = _output->getIterator(i);
            }
        }

        void writeTuple(vector <double> const& tuple, std::shared_ptr<Query>& query)
        {
            if (tuple.size() != _nAttrs)
            {
                throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "Wtf mate";
            }
            Coordinates chunkPosition = _outputCellPosition;
            _output->getArrayDesc().getChunkPositionFor(chunkPosition);
            if (chunkPosition[1] != _outputChunkPosition[1])  //first chunk, or a new chunk
            {
                _outputChunkPosition = chunkPosition;
                for (size_t i=0; i<_nAttrs; ++i)
                {
                    if (_outputChunkIterators[i] )
                    {
                        _outputChunkIterators[i]->flush();
                    }
                    int iterationMode = (i == 0 ? ChunkIterator::SEQUENTIAL_WRITE : ChunkIterator::SEQUENTIAL_WRITE |
                                                  ChunkIterator::NO_EMPTY_CHECK);
                    _outputChunkIterators[i] = _outputArrayIterators[i]->newChunk(chunkPosition).getIterator(query,
                                                                                                             iterationMode);
                }
            }
            for(size_t i=0; i<_nAttrs; ++i)
            {
                _dval.setDouble(tuple[i]);
                _outputChunkIterators[i]->setPosition(_outputCellPosition);
                _outputChunkIterators[i]->writeItem(_dval);
            }
            _outputCellPosition[1]++;
        }

        std::shared_ptr<Array> finalize()
        {
            for (size_t i=0; i<_nAttrs; ++i)
            {
                if(_outputChunkIterators[i])
                {
                    _outputChunkIterators[i]->flush();
                    _outputChunkIterators[i].reset();
                }
                _outputArrayIterators[i].reset();
            }
            return _output;
        }
    };

    virtual bool changesDistribution(vector<ArrayDesc> const& sourceSchemas) const
    {
        return true;
    }

	virtual RedistributeContext getOutputDistribution(vector<RedistributeContext> const& inputDistributions,
													vector<ArrayDesc> const& inputSchemas) const
    {
       return RedistributeContext(psUndefined);
    }

    std::shared_ptr< Array> execute(vector< std::shared_ptr< Array> >& inputArrays, std::shared_ptr<Query> query)
    {
        RExecSettings settings (_parameters, false, query);
        std::shared_ptr<Rconnection> rc (new Rconnection());
        int i=rc->connect();
        if (i)
        {
          i = ::system("setsid R CMD Rserve --vanilla >/dev/null 2>&1 &");
          i = rc->connect();
        }
        if(i)
        {
          throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION)<<"could not connect to R";
        }
        std::shared_ptr<Array> inputArray = inputArrays[0];
        ArrayDesc const& inputSchema = inputArray->getArrayDesc();
        AttributeID const nAttrs = inputSchema.getAttributes(true).size();
        AttributeID const nOutputAttrs = _schema.getAttributes(true).size();
        vector<string> attributeNames(nAttrs, "");
        vector<std::shared_ptr<ConstArrayIterator> > saiters(nAttrs);
        vector<std::shared_ptr<ConstChunkIterator> > sciters(nAttrs);
        for (AttributeID i = 0; i<nAttrs; ++i)
        {
            attributeNames[i] = inputSchema.getAttributes()[i].getName();
            saiters[i] = inputArray->getConstIterator(i);
        }
        OutputWriter outputArrayWriter(_schema, query);
        while (!saiters[0]->end())
        {
            for (AttributeID i = 0; i<nAttrs; ++i)
            {
                vector<double> inputData;
                sciters[i] = saiters[i]->getChunk().getConstIterator(ChunkIterator::IGNORE_EMPTY_CELLS);
                while( !sciters[i]->end())
                {
                    Value const& val = sciters[i]->getItem();
                    if (!val.isNull())
                    {
                        inputData.push_back(val.getDouble());
                    }
                    ++(*sciters[i]);
                }
                std::shared_ptr<Rdouble> rInput(new Rdouble(&inputData[0], inputData.size()));
                rc->assign(attributeNames[i].c_str(), rInput.get());
            }
            /* Note that we assume output from R is always a list. */
            std::shared_ptr<Rvector> rOutput((Rvector*) rc->eval(settings.rExpression().c_str()));
            if (!rOutput)
            {
                throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "Did not receive output from R";
            }
            if(rOutput->type != XT_VECTOR)
            {
                throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "The R expression did not return a list";
            }
            if (rOutput->getCount() < (int)nOutputAttrs)
            {
                throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "Returned list has fewer output attributes than requested";
            }
            vector<double*> outputs(nOutputAttrs);
            size_t nOutputs = 0;
            for(size_t i = 0; i<nOutputAttrs; ++i)
            {
                if(rOutput->byPos(i)->type != XT_ARRAY_DOUBLE)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "Returned list elements must be of type double";
                }
                if (nOutputs == 0)
                {
                    nOutputs = ((Rdouble*) rOutput->byPos(i))->length();
                }
                else if (((Rdouble*) rOutput->byPos(i))->length() != nOutputs)
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "Returned lists of mismatched size";
                }
                outputs[i] = ((Rdouble*) rOutput->byPos(i))->doubleArray();
            }
            for (size_t i =0; i<nOutputs; ++i)
            {
                vector<double> tuple (nOutputAttrs);
                for(size_t j=0; j<nOutputAttrs; ++j)
                {
                    tuple[j]=outputs[j][i];
                }
                outputArrayWriter.writeTuple(tuple,query);
            }
            for (AttributeID i = 0; i<nAttrs; ++i)
            {
                ++(*saiters[i]);
            }
        }
        return outputArrayWriter.finalize();
    }
};
REGISTER_PHYSICAL_OPERATOR_FACTORY(PhysicalRExec, "r_exec", "PhysicalRExec");

} //namespace scidb
