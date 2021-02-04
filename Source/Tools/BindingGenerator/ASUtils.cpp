//
// Copyright (c) 2008-2020 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "ASUtils.h"
#include "Tuning.h"
#include "Utils.h"
#include "XmlAnalyzer.h"
#include "XmlSourceData.h"

#include <cassert>
#include <regex>

namespace ASBindingGenerator
{

// https://www.angelcode.com/angelscript/sdk/docs/manual/doc_datatypes_primitives.html
// https://en.cppreference.com/w/cpp/language/types
string CppPrimitiveTypeToAS(const string& cppType)
{
    if (cppType == "bool")
        return "bool";

    if (cppType == "char" || cppType == "signed char")
        return "int8";

    if (cppType == "unsigned char")
        return "uint8";

    if (cppType == "short")
        return "int16";

    if (cppType == "unsigned short")
        return "uint16";

    if (cppType == "int")
        return "int";

    if (cppType == "unsigned" || cppType == "unsigned int")
        return "uint";

    if (cppType == "long long")
        return "int64";

    if (cppType == "unsigned long long")
        return "uint64";

    if (cppType == "float")
        return "float";

    if (cppType == "double")
        return "double";

    // Types below are registered in Manual.cpp
    
    if (cppType == "long")
        return "long";

    if (cppType == "unsigned long")
        return "ulong";

    if (cppType == "size_t")
        return "size_t";

    if (cppType == "SDL_JoystickID")
        return "SDL_JoystickID";

    throw Exception(cppType + " not a primitive type");
}

shared_ptr<EnumAnalyzer> FindEnum(const string& name)
{
    NamespaceAnalyzer namespaceAnalyzer(SourceData::namespaceUrho3D_);
    vector<EnumAnalyzer> enumAnalyzers = namespaceAnalyzer.GetEnums();

    for (const EnumAnalyzer& enumAnalyzer : enumAnalyzers)
    {
        if (enumAnalyzer.GetTypeName() == name)
            return make_shared<EnumAnalyzer>(enumAnalyzer);
    }

    return shared_ptr<EnumAnalyzer>();
}

static bool IsUsing(const string& identifier)
{
    for (xml_node memberdef : SourceData::usings_)
    {
        UsingAnalyzer usingAnalyzer(memberdef);
        
        if (usingAnalyzer.GetName() == identifier)
            return true;
    }

    return false;
}

bool IsKnownCppType(const string& name)
{
    static vector<string> _knownTypes = {
        "void",
        "bool",
        "size_t",
        "char",
        "signed char",
        "unsigned char",
        "short",
        "unsigned short",
        "int",
        "long",
        "unsigned",
        "unsigned int",
        "unsigned long",
        "long long",
        "unsigned long long",
        "float",
        "double",
        "SDL_JoystickID",

        // TODO: Remove
        "VariantMap",
    };

    if (CONTAINS(_knownTypes, name))
        return true;

    if (SourceData::classesByName_.find(name) != SourceData::classesByName_.end())
        return true;

    if (SourceData::enums_.find(name) != SourceData::enums_.end())
        return true;

    if (EndsWith(name, "Flags"))
        return true;

    return false;
}

shared_ptr<ClassAnalyzer> FindClassByName(const string& name)
{
    auto it = SourceData::classesByName_.find(name);
    if (it != SourceData::classesByName_.end())
    {
        xml_node compounddef = it->second;
        return make_shared<ClassAnalyzer>(compounddef);
    }

    // using VariantVector = Vector<Variant>

    return shared_ptr<ClassAnalyzer>();
}

shared_ptr<ClassAnalyzer> FindClassByID(const string& id)
{
    auto it = SourceData::classesByID_.find(id);
    if (it != SourceData::classesByID_.end())
    {
        xml_node compounddef = it->second;
        return make_shared<ClassAnalyzer>(compounddef);
    }

    // using VariantVector = Vector<Variant>

    return shared_ptr<ClassAnalyzer>();
}

// Variable name can be empty for function return type
ConvertedVariable CppVariableToAS(const TypeAnalyzer& type, const string& name, VariableUsage usage, string defaultValue)
{
    ConvertedVariable result;

    if (type.IsRvalueReference() || type.IsDoublePointer() || type.IsRefToPointer())
        throw Exception("Error: type \"" + type.ToString() + "\" can not automatically bind");

    string cppTypeName = type.GetNameWithTemplateParams();

    if (cppTypeName == "void" && !type.IsPointer() && usage == VariableUsage::FunctionReturn)
    {
        result.asDeclaration_ = "void";
        return result;
    }

    // Works with both Vector<String> and Vector<String>&
    if ((cppTypeName == "Vector<String>" || cppTypeName == "StringVector") && !type.IsPointer() && usage == VariableUsage::FunctionReturn)
    {
        result.asDeclaration_ = "Array<String>@";
        result.newCppDeclaration_ = "CScriptArray*";
        result.glue_ = "return VectorToArray<String>(result, \"Array<String>\");\n";
        return result;
    }

    smatch match;
    regex_match(cppTypeName, match, regex("SharedPtr<(\\w+)>"));
    if (match.size() == 2 && usage == VariableUsage::FunctionReturn)
    {
        string cppSubtypeName = match[1].str();

        string asSubtypeName;

        try
        {
            asSubtypeName = CppPrimitiveTypeToAS(cppSubtypeName);
        }
        catch (...)
        {
            asSubtypeName = cppSubtypeName;
        }

        if (cppSubtypeName == "WorkItem") // TODO autodetect
            throw Exception("Error: type \"" + type.ToString() + "\" can not automatically bind");

        result.asDeclaration_ = asSubtypeName + "@+";
        result.newCppDeclaration_ = cppSubtypeName + "*";
        result.glue_ = "return result.Detach();\n";
        return result;
    }

    regex_match(cppTypeName, match, regex("Vector<SharedPtr<(\\w+)>>"));
    if (match.size() == 2 && usage == VariableUsage::FunctionReturn)
    {
        string cppSubtypeName = match[1].str();

        string asSubtypeName;

        try
        {
            asSubtypeName = CppPrimitiveTypeToAS(cppSubtypeName);
        }
        catch (...)
        {
            asSubtypeName = cppSubtypeName;
        }

        result.asDeclaration_ = "Array<" + asSubtypeName + "@>@";
        result.newCppDeclaration_ = "CScriptArray*";

        // Which variant is correct/better?
#if 0
        result->glueResult_ = "return VectorToArray<SharedPtr<" + cppTypeName + "> >(result, \"Array<" + asTypeName + "@>@\");\n";
#else
        result.glue_ = "return VectorToHandleArray(result, \"Array<" + asSubtypeName + "@>\");\n";
#endif
        return result;
    }

    regex_match(cppTypeName, match, regex("PODVector<(\\w+)\\*>"));
    if (match.size() == 2 && usage == VariableUsage::FunctionReturn)
    {
        string cppSubtypeName = match[1].str();

        string asSubtypeName;

        try
        {
            asSubtypeName = CppPrimitiveTypeToAS(cppSubtypeName);
        }
        catch (...)
        {
            asSubtypeName = cppSubtypeName;
        }

        result.asDeclaration_ = "Array<" + asSubtypeName + "@>@";
        result.newCppDeclaration_ = "CScriptArray*";
        result.glue_ = "return VectorToHandleArray(result, \"Array<" + asSubtypeName + "@>\");\n";
        return result;
    }

    regex_match(cppTypeName, match, regex("PODVector<(\\w+)>"));
    if (match.size() == 2 && type.IsConst() == type.IsReference() && usage == VariableUsage::FunctionReturn)
    {
        string cppSubtypeName = match[1].str();

        string asSubtypeName;

        try
        {
            asSubtypeName = CppPrimitiveTypeToAS(cppSubtypeName);
        }
        catch (...)
        {
            asSubtypeName = cppSubtypeName;
        }

        result.asDeclaration_ = "Array<" + asSubtypeName + ">@";
        result.newCppDeclaration_ = "CScriptArray*";
        result.glue_ = "return VectorToArray(result, \"Array<" + asSubtypeName + ">\");\n";
        return result;
    }

    // =============================================================================
    
    if (cppTypeName == "Context" && usage == VariableUsage::FunctionParameter)
        throw Exception("Context can be used as firs parameter of constructors only");

    if (cppTypeName == "Vector<String>" && type.IsConst() && type.IsReference() && usage == VariableUsage::FunctionParameter)
    {
        string newCppVarName = name + "_conv";
        //result->asDecl_ = "String[]&";
        result.asDeclaration_ = "Array<String>@+";
        result.newCppDeclaration_ = "CScriptArray* " + newCppVarName;
        result.glue_ = "    " + cppTypeName + " " + name + " = ArrayToVector<String>(" + newCppVarName + ");\n";

        if (!defaultValue.empty())
        {
            assert(defaultValue == "Vector< String >()");
            //result->asDecl_ += " = Array<String>()";
            result.asDeclaration_ += " = null";
        }

        return result;
    }

    regex_match(cppTypeName, match, regex("PODVector<(\\w+)>"));
    if (match.size() == 2 && type.IsConst() && type.IsReference() && usage == VariableUsage::FunctionParameter)
    {
        string cppSubtypeName = match[1].str();

        string asSubtypeName;

        try
        {
            asSubtypeName = CppPrimitiveTypeToAS(cppSubtypeName);
        }
        catch (...)
        {
            asSubtypeName = cppSubtypeName;
        }

        string newCppVarName = name + "_conv";
        result.asDeclaration_ = "Array<" + asSubtypeName + ">@+";
        result.newCppDeclaration_ = "CScriptArray* " + newCppVarName;
        result.glue_ = "    " + cppTypeName + " " + name + " = ArrayToPODVector<" + cppSubtypeName + ">(" + newCppVarName + ");\n";

        assert(defaultValue.empty()); // TODO: make

        return result;
    }

    regex_match(cppTypeName, match, regex("PODVector<(\\w+)\\*>"));
    // TODO check \\w is refcounted
    if (match.size() == 2 && type.IsConst() && type.IsReference() && usage == VariableUsage::FunctionParameter)
    {
        string cppSubtypeName = match[1].str();

        string asSubtypeName;

        try
        {
            asSubtypeName = CppPrimitiveTypeToAS(cppSubtypeName);
        }
        catch (...)
        {
            asSubtypeName = cppSubtypeName;
        }

        string newCppVarName = name + "_conv";
        result.asDeclaration_ = "Array<" + asSubtypeName + "@>@";
        result.newCppDeclaration_ = "CScriptArray* " + newCppVarName;
        result.glue_ = "    " + cppTypeName + " " + name + " = ArrayToPODVector<" + cppSubtypeName + "*>(" + newCppVarName + ");\n";

        assert(defaultValue.empty()); // TODO: make

        return result;
    }

    regex_match(cppTypeName, match, regex("Vector<SharedPtr<(\\w+)>>"));
    if (match.size() == 2 && type.IsConst() && type.IsReference() && usage == VariableUsage::FunctionParameter)
    {
        string cppSubtypeName = match[1].str();

        string asSubtypeName;

        try
        {
            asSubtypeName = CppPrimitiveTypeToAS(cppSubtypeName);
        }
        catch (...)
        {
            asSubtypeName = cppSubtypeName;
        }

        if (cppSubtypeName == "WorkItem") // TODO autodetect
            throw Exception("Error: type \"" + type.ToString() + "\" can not automatically bind");

        string newCppVarName = name + "_conv";
        result.asDeclaration_ = "Array<" + asSubtypeName + "@>@+";
        result.newCppDeclaration_ = "CScriptArray* " + newCppVarName;
        result.glue_ = "    " + cppTypeName + " " + name + " = HandleArrayToVector<" + cppSubtypeName + ">(" + newCppVarName + ");\n";
        
        assert(defaultValue.empty()); // TODO: make

        return result;
    }

    // =============================================================================

    if (cppTypeName == "Context" && usage == VariableUsage::FunctionReturn)
        throw Exception("Error: type \"" + type.ToString() + "\" can not be returned");

    if (!IsKnownCppType(cppTypeName))
        throw Exception("Error: type \"" + type.ToString() + "\" can not automatically bind");

    shared_ptr<ClassAnalyzer> analyzer = FindClassByName(cppTypeName);
    if (analyzer && analyzer->IsInternal())
        throw Exception("Error: type \"" + type.ToString() + "\" can not automatically bind bacause internal");

    if (analyzer && Contains(analyzer->GetComment(), "NO_BIND"))
        throw Exception("Error: type \"" + cppTypeName + "\" can not automatically bind bacause have @nobind mark");

    // analyzer can be null for simple types (int, float) or if type "using VariantVector = Vector<Variant>"
    // TODO add to type info "IsUsing"
    // TODO add description to TypeAnalyzer::GetClass()

    if (IsUsing(cppTypeName) && cppTypeName != "VariantMap")
        throw Exception("Using \"" + cppTypeName + "\" can not automatically bind");

    string asTypeName;

    try
    {
        asTypeName = CppPrimitiveTypeToAS(cppTypeName);
    }
    catch (...)
    {
        asTypeName = cppTypeName;
    }

    if (asTypeName == "void" && type.IsPointer())
        throw Exception("Error: type \"void*\" can not automatically bind");

    if (asTypeName.find('<') != string::npos)
        throw Exception("Error: type \"" + type.ToString() + "\" can not automatically bind");

    if (Contains(type.ToString(), "::"))
        throw Exception("Error: type \"" + type.ToString() + "\" can not automatically bind bacause internal");

    if (type.IsConst() && type.IsReference() && usage == VariableUsage::FunctionParameter)
    {
        result.asDeclaration_ = "const " + asTypeName + "&in";

        if (!defaultValue.empty())
        {
            defaultValue = CppValueToAS(defaultValue);
            defaultValue = ReplaceAll(defaultValue, "\"", "\\\"");
            result.asDeclaration_ += " = " + defaultValue;
        }

        //if (!name.empty())
        //    result.asDeclaration_ += result.asDeclaration_ + " " + name;
        
        return result;
    }

    result.asDeclaration_ = asTypeName;

    if (type.IsReference())
    {
        result.asDeclaration_ += "&";
    }
    else if (type.IsPointer())
    {
        shared_ptr<ClassAnalyzer> analyzer = FindClassByName(cppTypeName);

        if (analyzer && (analyzer->IsRefCounted() || Contains(analyzer->GetComment(), "FAKE_REF")))
            result.asDeclaration_ += "@+";
        else
            throw Exception("Error: type \"" + type.ToString() + "\" can not automatically bind");
    }

    if (usage == VariableUsage::FunctionReturn && type.IsConst() && !type.IsPointer())
        result.asDeclaration_ = "const " + result.asDeclaration_;

    //if (!name.empty())
    //    result.asDeclaration_ += result.asDeclaration_ + " " + name;

    if (!defaultValue.empty())
    {
        defaultValue = CppValueToAS(defaultValue);
        defaultValue = ReplaceAll(defaultValue, "\"", "\\\"");
        result.asDeclaration_ += " = " + defaultValue;
    }

    return result;
}

string CppTypeToAS(const TypeAnalyzer& type, TypeUsage typeUsage)
{
    if (type.IsRvalueReference() || type.IsDoublePointer() || type.IsRefToPointer())
        throw Exception("Error: type \"" + type.ToString() + "\" can not automatically bind");

    string cppTypeName = type.GetNameWithTemplateParams();

    if (cppTypeName == "Context" && typeUsage == TypeUsage::FunctionReturn)
        throw Exception("Error: type \"" + type.ToString() + "\" can not be returned");

    if (!IsKnownCppType(type.GetNameWithTemplateParams()))
        throw Exception("Error: type \"" + type.ToString() + "\" can not automatically bind");

    shared_ptr<ClassAnalyzer> analyzer = FindClassByName(type.GetNameWithTemplateParams());
    if (analyzer && analyzer->IsInternal())
        throw Exception("Error: type \"" + type.ToString() + "\" can not automatically bind bacause internal");

    if (analyzer && Contains(analyzer->GetComment(), "NO_BIND"))
        throw Exception("Error: type \"" + cppTypeName + "\" can not automatically bind bacause have @nobind mark");

    // analyzer can be null for simple types (int, float) or if type "using VariantVector = Vector<Variant>"
    // TODO add to type info "IsUsing"
    // TODO add description to TypeAnalyzer::GetClass()

    if (IsUsing(cppTypeName) && cppTypeName != "VariantMap")
        throw Exception("Using \"" + cppTypeName + "\" can not automatically bind");

    string asTypeName;
    
    try
    {
        asTypeName = CppPrimitiveTypeToAS(cppTypeName);
    }
    catch (...)
    {
        asTypeName = cppTypeName;
    }

    if (asTypeName == "void" && type.IsPointer())
        throw Exception("Error: type \"void*\" can not automatically bind");

    if (asTypeName.find('<') != string::npos)
        throw Exception("Error: type \"" + type.ToString() + "\" can not automatically bind");

    if (Contains(type.ToString(), "::"))
        throw Exception("Error: type \"" + type.ToString() + "\" can not automatically bind bacause internal");

    if (type.IsConst() && type.IsReference() && typeUsage == TypeUsage::FunctionParameter)
        return "const " + asTypeName + "&in";

    string result = asTypeName;

    if (type.IsReference())
    {
        result += "&";
    }
    else if (type.IsPointer())
    {
        shared_ptr<ClassAnalyzer> analyzer = FindClassByName(type.GetNameWithTemplateParams());

        if (analyzer && (analyzer->IsRefCounted() || Contains(analyzer->GetComment(), "FAKE_REF")))
            result += "@+";
        else
            throw Exception("Error: type \"" + type.ToString() + "\" can not automatically bind");
    }

    if (typeUsage == TypeUsage::FunctionReturn && type.IsConst() && !type.IsPointer())
        result = "const " + result;

    return result;
}

string CppValueToAS(const string& cppValue)
{
    if (cppValue == "nullptr")
        return "null";

    if (cppValue == "Variant::emptyVariantMap")
        return "VariantMap()";

    if (cppValue == "NPOS")
        return "String::NPOS";

    return cppValue;
}

// =================================================================================

static string GenerateFunctionWrapperName(xml_node memberdef)
{
    string result = ExtractName(memberdef);

    vector<ParamAnalyzer> params = ExtractParams(memberdef);

    if (params.size() == 0)
    {
        result += "_void";
    }
    else
    {
        for (ParamAnalyzer param : params)
        {
            string t = param.GetType().GetNameWithTemplateParams();
            t = ReplaceAll(t, " ", "");
            t = ReplaceAll(t, "::", "");
            t = ReplaceAll(t, "<", "");
            t = ReplaceAll(t, ">", "");
            t = ReplaceAll(t, "*", "");
            result += "_" + t;
        }
    }

    return result;
}

string GenerateWrapperName(const GlobalFunctionAnalyzer& functionAnalyzer)
{
    return GenerateFunctionWrapperName(functionAnalyzer.GetMemberdef());
}

string GenerateWrapperName(const ClassStaticFunctionAnalyzer& functionAnalyzer)
{
    return functionAnalyzer.GetClassName() + "_" + GenerateFunctionWrapperName(functionAnalyzer.GetMemberdef());
}

string GenerateWrapperName(const ClassFunctionAnalyzer& functionAnalyzer, bool templateVersion)
{
    if (templateVersion)
        return functionAnalyzer.GetClassName() + "_" + GenerateFunctionWrapperName(functionAnalyzer.GetMemberdef()) + "_template";
    else
        return functionAnalyzer.GetClassName() + "_" + GenerateFunctionWrapperName(functionAnalyzer.GetMemberdef());
}

// =================================================================================

string GenerateWrapper(const GlobalFunctionAnalyzer& functionAnalyzer, const vector<ConvertedVariable>& convertedParams, const ConvertedVariable& convertedReturn)
{
    string result;

    string glueReturnType;

    if (!convertedReturn.newCppDeclaration_.empty())
        glueReturnType = convertedReturn.newCppDeclaration_;
    else
        glueReturnType = functionAnalyzer.GetReturnType().ToString();

    vector<ParamAnalyzer> params = functionAnalyzer.GetParams();
    
    result = "static " + glueReturnType + " " + GenerateWrapperName(functionAnalyzer) + "(";

    for (size_t i = 0; i < convertedParams.size(); i++)
    {
        if (i != 0)
            result += ", ";

        string paramDecl;

        if (!convertedParams[i].newCppDeclaration_.empty())
            paramDecl = convertedParams[i].newCppDeclaration_;
        else
            paramDecl = params[i].GetType().ToString() + " " + params[i].GetDeclname();

        result += paramDecl;
    }

    result +=
        ")\n"
        "{\n";

    for (size_t i = 0; i < convertedParams.size(); i++)
        result += convertedParams[i].glue_;

    if (glueReturnType != "void")
        result += "    " + functionAnalyzer.GetReturnType().ToString() + " result = ";
    else
        result += "    ";

    result += functionAnalyzer.GetName() + "(";

    for (size_t i = 0; i < convertedParams.size(); i++)
    {
        if (i != 0)
            result += ", ";

        result += params[i].GetDeclname();
    }

    result += ");\n";

    if (!convertedReturn.glue_.empty())
        result += "    " + convertedReturn.glue_;
    else if (glueReturnType != "void")
        result += "    return result;\n";

    result += "}";

    return result;
}

string GenerateWrapper(const ClassStaticFunctionAnalyzer& functionAnalyzer, const vector<ConvertedVariable>& convertedParams, const ConvertedVariable& convertedReturn)
{
    string result;

    string glueReturnType;

    if (!convertedReturn.newCppDeclaration_.empty())
        glueReturnType = convertedReturn.newCppDeclaration_;
    else
        glueReturnType = functionAnalyzer.GetReturnType().ToString();
    
    string insideDefine = InsideDefine(functionAnalyzer.GetHeaderFile());

    if (!insideDefine.empty())
        result += "#ifdef " + insideDefine + "\n";

    result +=
        "// " + functionAnalyzer.GetLocation() + "\n"
        "static " + glueReturnType + " " + GenerateWrapperName(functionAnalyzer) + "(";

    vector<ParamAnalyzer> params = functionAnalyzer.GetParams();

    for (size_t i = 0; i < convertedParams.size(); i++)
    {
        if (i != 0)
            result += ", ";

        string paramDecl;

        if (!convertedParams[i].newCppDeclaration_.empty())
            paramDecl = convertedParams[i].newCppDeclaration_;
        else
            paramDecl = params[i].GetType().ToString() + " " + params[i].GetDeclname();

        result += paramDecl;
    }

    result +=
        ")\n"
        "{\n";

    for (size_t i = 0; i < convertedParams.size(); i++)
        result += convertedParams[i].glue_;

    if (glueReturnType != "void")
        result += "    " + functionAnalyzer.GetReturnType().ToString() + " result = ";
    else
        result += "    ";

    result += functionAnalyzer.GetClassName() + "::" + functionAnalyzer.GetName() + "(";

    for (size_t i = 0; i < convertedParams.size(); i++)
    {
        if (i != 0)
            result += ", ";

        result += params[i].GetDeclname();
    }

    result += ");\n";

    if (!convertedReturn.glue_.empty())
        result += "    " + convertedReturn.glue_;
    else if (glueReturnType != "void")
        result += "    return result;\n";

    result += "}\n";

    if (!insideDefine.empty())
        result += "#endif\n";

    result += "\n";

    return result;
}

string GenerateWrapper(const ClassFunctionAnalyzer& functionAnalyzer, bool templateVersion, const vector<ConvertedVariable>& convertedParams, const ConvertedVariable& convertedReturn)
{
    string result;

    string insideDefine = InsideDefine(functionAnalyzer.GetClass().GetHeaderFile());

    if (!insideDefine.empty())
        result += "#ifdef " + insideDefine + "\n";

    string glueReturnType;

    if (!convertedReturn.newCppDeclaration_.empty())
        glueReturnType = convertedReturn.newCppDeclaration_;
    else
        glueReturnType = functionAnalyzer.GetReturnType().ToString();

    result +=
        "// " + functionAnalyzer.GetLocation() + "\n"
        "static " + glueReturnType + " " + GenerateWrapperName(functionAnalyzer, templateVersion) + "(" + functionAnalyzer.GetClassName() + "* ptr";

    vector<ParamAnalyzer> params = functionAnalyzer.GetParams();

    for (size_t i = 0; i < convertedParams.size(); i++)
    {
        result += ", ";

        string paramDecl;

        if (!convertedParams[i].newCppDeclaration_.empty())
            paramDecl = convertedParams[i].newCppDeclaration_;
        else
            paramDecl = params[i].GetType().ToString() + " " + params[i].GetDeclname();

        result += paramDecl;
    }

    result +=
        ")\n"
        "{\n";

    for (size_t i = 0; i < convertedParams.size(); i++)
        result += convertedParams[i].glue_;

    if (glueReturnType != "void")
        result += "    " + functionAnalyzer.GetReturnType().ToString() + " result = ";
    else
        result += "    ";

    result += "ptr->" + functionAnalyzer.GetName() + "(";

    for (size_t i = 0; i < convertedParams.size(); i++)
    {
        if (i != 0)
            result += ", ";

        result += params[i].GetDeclname();
    }

    result += ");\n";

    if (!convertedReturn.glue_.empty())
        result += "    " + convertedReturn.glue_;
    else if (glueReturnType != "void")
        result += "    return result;\n";

    result += "}\n";

    if (!insideDefine.empty())
        result += "#endif\n";

    result += "\n";

    return result;
}

// =================================================================================

string Generate_asFUNCTIONPR(const GlobalFunctionAnalyzer& functionAnalyzer)
{
    string functionName = functionAnalyzer.GetName();
    string cppParams = "(" + JoinParamsTypes(functionAnalyzer.GetMemberdef(), functionAnalyzer.GetSpecialization()) + ")";
    string returnType = functionAnalyzer.GetReturnType().ToString();
    return "asFUNCTIONPR(" + functionName + ", " + cppParams + ", " + returnType + ")";
}

string Generate_asFUNCTIONPR(const ClassStaticFunctionAnalyzer& functionAnalyzer)
{
    string className = functionAnalyzer.GetClassName();
    string functionName = functionAnalyzer.GetName();
    string cppParams = "(" + JoinParamsTypes(functionAnalyzer.GetMemberdef(), functionAnalyzer.GetSpecialization()) + ")";
    string returnType = functionAnalyzer.GetReturnType().ToString();
    return "asFUNCTIONPR(" + className + "::" + functionName + ", " + cppParams + ", " + returnType + ")";
}

string Generate_asMETHODPR(const ClassFunctionAnalyzer& functionAnalyzer, bool templateVersion)
{
    string className = functionAnalyzer.GetClassName();
    string functionName = functionAnalyzer.GetName();

    string cppParams = "(" + JoinParamsTypes(functionAnalyzer.GetMemberdef(), functionAnalyzer.GetSpecialization()) + ")";

    if (functionAnalyzer.IsConst())
        cppParams += " const";

    string returnType = functionAnalyzer.GetReturnType().ToString();
    
    if (templateVersion)
        return "asMETHODPR(T, " + functionName + ", " + cppParams + ", " + returnType + ")";
    else
        return "asMETHODPR(" + className + ", " + functionName + ", " + cppParams + ", " + returnType + ")";
}

} // namespace ASBindingGenerator
