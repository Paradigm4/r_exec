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

#include "query/Operator.h"
#include "RExecSettings.h"

namespace scidb //not required
{

using namespace std; // -> SciDB 15.7

class LogicalRExec : public LogicalOperator
{
public:
    LogicalRExec(const string& logicalName, const string& alias):
        LogicalOperator(logicalName, alias)
    {
        ADD_PARAM_INPUT()
        ADD_PARAM_VARIES()
    }

    vector<std::shared_ptr<OperatorParamPlaceholder> > nextVaryParamPlaceholder(vector< ArrayDesc> const& schemas)
    {
        vector<std::shared_ptr<OperatorParamPlaceholder> > res;
        res.push_back(END_OF_VARIES_PARAMS());
        if (_parameters.size() < RExecSettings::MAX_PARAMETERS)
        {
            res.push_back(PARAM_CONSTANT(TID_STRING));
        }
        return res;
    }

    void checkInputAttributes(ArrayDesc const& inputSchema)
    {
        Attributes const& attrs = inputSchema.getAttributes(true);
        size_t const nAttrs = attrs.size();
        for (AttributeID i =0; i<nAttrs; ++i)
        {
            if (attrs[i].getType() != TID_DOUBLE)
            {
                throw SYSTEM_EXCEPTION(SCIDB_SE_OPERATOR, SCIDB_LE_ILLEGAL_OPERATION)
                      << "r_exec only accepts an input with attributes of type double";
            }
            for (AttributeID j = i+1; j<nAttrs; ++j)
            {
                if(attrs[i].getName() == attrs[j].getName())
                {
                    throw SYSTEM_EXCEPTION(SCIDB_SE_OPERATOR, SCIDB_LE_ILLEGAL_OPERATION)
                                          << "Attributes input to r_exec must have unique primary names; insert a cast() operator to make it so";
                }
            }
        }
    }

    ArrayDesc inferSchema(vector< ArrayDesc> schemas, std::shared_ptr< Query> query)
    {
        ArrayDesc const& inputSchema = schemas[0];
        checkInputAttributes(inputSchema);
        RExecSettings settings (_parameters, true, query);
        Attributes outputAttributes;
        for (size_t i =0; i<settings.numOutputAttrs(); ++i)
        {
            ostringstream attributeName;
            attributeName<<"expr_value_"<<i;
            outputAttributes.push_back( AttributeDesc(0, attributeName.str(), TID_DOUBLE, 0, 0 ));
        }
        outputAttributes = addEmptyTagAttribute(outputAttributes);
        Dimensions outputDimensions;
        outputDimensions.push_back(DimensionDesc("inst", 0, query->getInstancesCount()-1, 1, 0)); 
		outputDimensions.push_back(DimensionDesc("n",    0, CoordinateBounds::getMax(), settings.outputChunkSize(), 0)); // -> SciDB 15.7
		return ArrayDesc(inputSchema.getName(), outputAttributes, outputDimensions,defaultPartitioning()); // -> SciDB 15.7
    }
};

REGISTER_LOGICAL_OPERATOR_FACTORY(LogicalRExec, "r_exec");

}
