#define CLASSES_MISC_C
#include <sc_memmgr.h>
#include <stdlib.h>
#include "classes.h"

#include <sc_trace_fprintf.h>
#include "class_strings.h"

/** \file classes_misc.c
** FedEx parser output module for generating C++  class definitions
** December  5, 1989
** release 2 17-Feb-1992
** release 3 March 1993
** release 4 December 1993
** K. C. Morris
**
** Development of FedEx was funded by the United States Government,
** and is not subject to copyright.

*******************************************************************
The conventions used in this binding follow the proposed specification
for the STEP Standard Data Access Interface as defined in document
N350 ( August 31, 1993 ) of ISO 10303 TC184/SC4/WG7.
*******************************************************************/

extern int multiple_inheritance;

/**
 * creates a file for c++ header definitions, with name filename
 * Returns:  FILE* pointer to file created or NULL
 * Status:  complete
 */
FILE *FILEcreate(const char *filename)
{
    FILE *file;
    const char *fn;

    if((file = fopen(filename, "w")) == NULL) {
        fprintf(stderr, "**Error in SCHEMAprint:  unable to create file %s ** \n", filename);
        return (NULL);
    }

    fn = StrToConstant(filename);
    fprintf(file, "#ifndef  %s\n", fn);
    fprintf(file, "#define  %s\n\n", fn);

    fprintf(file, "// This file was generated by exp2cxx,\n// %s.\n", SC_VERSION);
    fprintf(file, "// You probably don't want to edit it since your modifications\n");
    fprintf(file, "// will be lost if exp2cxx is used to regenerate it.\n\n");
    return (file);

}

/** closes a file opened with FILEcreate */
void FILEclose(FILE *file)
{
    fprintf(file, "#endif\n");
    fclose(file);
}


/**  indicates whether the attribute is an aggregate */
int isAggregate(Variable a)
{
    return(TYPEinherits_from(VARget_type(a), aggregate_));
}

/**  indicates whether the attribute is an aggregate */
int isAggregateType(const Type t)
{
    return(TYPEinherits_from(t, aggregate_));
}

/** \returns a pointer to a static buffer, containing a string which is the type used by the c++ data member access functions */
const char *AccessType(Type t)
{
    Class_Of_Type class;
    static char nm [BUFSIZ+1];
    strncpy(nm, TypeName(t), BUFSIZ - 4);
    if(TYPEis_entity(t)) {
        strcat(nm, "_ptr");
        return nm;
    } else if(TYPEis_select(t) || TYPEis_aggregate(t)) {
        strcat(nm, "_ptr");
        return nm;
    }
    class = TYPEget_type(t);
    if(class == enumeration_) {
        strncpy(nm, TypeName(t), BUFSIZ - 2);
        strcat(nm, "_var");
        return nm;
    }
    if(class == logical_) {
        strncpy(nm, "Logical", BUFSIZ - 2);
    }

    /*    case TYPE_BOOLEAN:    */
    if(class == boolean_) {
        strncpy(nm, "Boolean", BUFSIZ - 2);
    }
    return nm;
}

/** \returns  pointer to a static buffer, containing a new capitalized name
 *
 * creates a new name with first character's in caps
 */
const char *PrettyTmpName(const char *oldname)
{
    int i = 0;
    static char newname [BUFSIZ+1];
    newname [0] = '\0';
    while((oldname [i] != '\0') && (i < BUFSIZ - 1)) {
        newname [i] = ToLower(oldname [i]);
        if(oldname [i] == '_') {   /*  character is '_'   */
            ++i;
            newname [i] = ToUpper(oldname [i]);
        }
        if(oldname [i] != '\0') {
            ++i;
        }
    }

    newname [0] = ToUpper(oldname [0]);
    newname [i] = '\0';
    return newname;
}

/* This function is out of date DAS */
const char *EnumName(const char *oldname)
{
    int j = 0;
    static char newname [MAX_LEN];
    if(!oldname) {
        return ("");
    }

    strcpy(newname, ENUM_PREFIX);
    j = strlen(ENUM_PREFIX);
    newname [j] = ToUpper(oldname [0]);
    strncpy(newname + j + 1, StrToLower(oldname + 1), MAX_LEN - j - 1);
    j = strlen(newname);
    newname [j] = '\0';
    return (newname);
}

const char *SelectName(const char *oldname)
{
    int j = 0;
    static char newname [MAX_LEN];
    if(!oldname) {
        return ("");
    }

    strcpy(newname, TYPE_PREFIX);
    newname [0] = ToUpper(newname [0]);
    j = strlen(TYPE_PREFIX);
    newname [j] = ToUpper(oldname [0]);
    strncpy(newname + j + 1, StrToLower(oldname + 1), MAX_LEN - j - 1);
    j = strlen(newname);
    newname [j] = '\0';
    return (newname);
}

/** \return fundamental type but as the string which corresponds to the appropriate type descriptor
 * if report_reftypes is true, report REFERENCE_TYPE when appropriate
 */
const char *FundamentalType(const Type t, int report_reftypes)
{
    if(report_reftypes && TYPEget_head(t)) {
        return("REFERENCE_TYPE");
    }
    switch(TYPEget_body(t)->type) {
        case integer_:
            return("sdaiINTEGER");
        case real_:
            return("sdaiREAL");
        case string_:
            return("sdaiSTRING");
        case binary_:
            return("sdaiBINARY");
        case boolean_:
            return("sdaiBOOLEAN");
        case logical_:
            return("sdaiLOGICAL");
        case number_:
            return("sdaiNUMBER");
        case generic_:
            return("GENERIC_TYPE");
        case aggregate_:
            return("AGGREGATE_TYPE");
        case array_:
            return("ARRAY_TYPE");
        case bag_:
            return("BAG_TYPE");
        case set_:
            return("SET_TYPE");
        case list_:
            return("LIST_TYPE");
        case entity_:
            return("sdaiINSTANCE");
        case enumeration_:
            return("sdaiENUMERATION");
        case select_:
            return ("sdaiSELECT");
        default:
            return("UNKNOWN_TYPE");
    }
}

/** this actually gets you the name of the variable that will be generated to be a
 * TypeDescriptor or subtype of TypeDescriptor to represent Type t in the dictionary.
 */
const char *TypeDescriptorName(Type t)
{
    static char b [BUFSIZ+1];
    Schema parent = t->superscope;
    /* NOTE - I corrected a prev bug here in which the *current* schema was
    ** passed to this function.  Now we take "parent" - the schema in which
    ** Type t was defined - which was actually used to create t's name. DAR */

    if(!parent) {
        parent = TYPEget_body(t)->entity->superscope;
        /* This works in certain cases that don't work otherwise (basically a
        ** kludge).  For some reason types which are really entity choices of
        ** a select have no superscope value, but their super may be tracked
        ** by following through the entity they reference, as above. */
    }

    sprintf(b, "%s::%s%s", SCHEMAget_name(parent), TYPEprefix(t),
            TYPEget_name(t));
    return b;
}

/** this gets you the name of the type of TypeDescriptor (or subtype) that a
 * variable generated to represent Type t would be an instance of.
 */
const char *GetTypeDescriptorName(Type t)
{
    switch(TYPEget_body(t)->type) {
        case aggregate_:
            return "AggrTypeDescriptor";

        case list_:
            return "ListTypeDescriptor";

        case set_:
            return "SetTypeDescriptor";

        case bag_:
            return "BagTypeDescriptor";

        case array_:
            return "ArrayTypeDescriptor";

        case select_:
            return "SelectTypeDescriptor";

        case boolean_:
        case logical_:
        case enumeration_:
            return "EnumTypeDescriptor";

        case entity_:
            return "EntityDescriptor";

        case integer_:
        case real_:
        case string_:
        case binary_:
        case number_:
        case generic_:
            return "TypeDescriptor";
        default:
            fprintf(stderr, "Error at %s:%d - type %d not handled by switch statement.", __FILE__, __LINE__, TYPEget_body(t)->type);
            abort();
    }
    /* NOTREACHED */
    return "";
}

int ENTITYhas_explicit_attributes(Entity e)
{
    Linked_List l = ENTITYget_attributes(e);
    int cnt = 0;
    LISTdo(l, a, Variable)
    if(VARget_initializer(a) == EXPRESSION_NULL) {
        ++cnt;
    }
    LISTod;
    return cnt;

}

Entity ENTITYput_superclass(Entity entity)
{
#define ENTITYget_type(e)  ((e)->u.entity->type)

    Linked_List l = ENTITYget_supertypes(entity);
    EntityTag tag;

    if(! LISTempty(l)) {
        Entity super = 0;

        if(multiple_inheritance) {
            Linked_List list = 0;
            list = ENTITYget_supertypes(entity);
            if(! LISTempty(list)) {
                /* assign superclass to be the first one on the list of parents */
                super = (Entity)LISTpeek_first(list);
            }
        } else {
            Entity ignore = 0;
            int super_cnt = 0;
            /* find the first parent that has attributes (in the parent or any of its
            ancestors).  Make super point at that parent and print warnings for
             all the rest of the parents. DAS */
            LISTdo(l, e, Entity)
            /*  if there's no super class yet,
            or if the entity super class [the variable] super is pointing at
            doesn't have any attributes: make super point at the current parent.
            As soon as the parent pointed to by super has attributes, stop
            assigning super and print ignore messages for the remaining parents.
            */
            if((! super) || (! ENTITYhas_explicit_attributes(super))) {
                ignore = super;
                super = e;
                ++ super_cnt;
            }  else {
                ignore = e;
            }
            if(ignore) {
                fprintf(stderr, "WARNING:  multiple inheritance not implemented. In ENTITY %s, SUPERTYPE %s ignored.\n", ENTITYget_name(entity), ENTITYget_name(e));
            }
            LISTod;
        }

        tag = (EntityTag) sc_malloc(sizeof(struct EntityTag_));
        tag -> superclass = super;
        TYPEput_clientData(ENTITYget_type(entity), (ClientData) tag);
        return super;
    }
    return 0;
}

Entity ENTITYget_superclass(Entity entity)
{
    EntityTag tag;
    tag = (EntityTag) TYPEget_clientData(ENTITYget_type(entity));
    return (tag ? tag -> superclass : 0);
}

void ENTITYget_first_attribs(Entity entity, Linked_List result)
{
    Linked_List supers;

    LISTdo(ENTITYget_attributes(entity), attr, void *)
    LISTadd_last(result, attr);
    LISTod;
    supers = ENTITYget_supertypes(entity);
    if(supers) {
        ENTITYget_first_attribs((Entity)LISTget_first(supers), result);
    }
}

/** Attributes are divided into four categories:
** these are not exclusive as far as I can tell! I added defs below DAS
**
**  . simple explicit
**
**  . type shifters    // not DERIVEd - redefines type in ancestor
**                     // VARget_initializer(v) returns null
**
**  . simple derived   // DERIVEd - is calculated - VARget_initializer(v)
**                     // returns non-zero, VARis_derived(v) is non-zero
**
**  . overriding       // includes type shifters and derived
**
** All of them are added to the dictionary.
** Only type shifters generate a new STEPattribute.
** Type shifters generate access functions and data members, for now.
** Overriding generate access functions and data members, for now. ???? DAS
**
**   type shifting attributes
**   ------------------------
**  before printing new STEPattribute
**  check to see if it's already printed in supertype
**  still add new access function
**
**   overriding attributes
**   ---------------------
**  go through derived attributes
**  if STEPattribute found with same name
**  tell it to be * for reading and writing
**/
Variable VARis_type_shifter(Variable a)
{
    char *temp;
    if(VARis_derived(a) || VARget_inverse(a)) {
        return 0;
    }
    temp = EXPRto_string(VARget_name(a));
    if(! strncmp(StrToLower(temp), "self\\", 5)) {
        /*    a is a type shifter */
        sc_free(temp);
        return a;
    }
    sc_free(temp);
    return 0;
}

Variable VARis_overrider(Entity e, Variable a)
{
    Variable other;
    char *tmp;
    tmp = VARget_simple_name(a);
    LISTdo(ENTITYget_supertypes(e), s, Entity)
    if((other = ENTITYget_named_attribute(s, tmp))
            && other != a) {
        return other;
    }
    LISTod;
    return 0;
}

/** For a renamed type, returns the original (ancestor) type
 * from which t descends.  Return NULL if t is top level.
 */
Type TYPEget_ancestor(Type t)
{
    Type i = t;

    if(!TYPEget_head(i)) {
        return NULL;
    }

    while(TYPEget_head(i)) {
        i = TYPEget_head(i);
    }

    return i;
}