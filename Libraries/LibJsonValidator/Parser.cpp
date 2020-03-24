/*
 * Copyright (c) 2020, Emanuel Sprung <emanuel.sprung@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <LibCore/File.h>
#include <LibJsonValidator/JsonSchemaNode.h>
#include <LibJsonValidator/Parser.h>
#include <stdio.h>

namespace JsonValidator {

Parser::Parser()
{
}

Parser::~Parser()
{
}

JsonValue Parser::run(const FILE* fd)
{
    StringBuilder builder;
    for (;;) {
        char buffer[1024];
        if (!fgets(buffer, sizeof(buffer), const_cast<FILE*>(fd)))
            break;
        builder.append(buffer);
    }

    JsonValue schema_json = JsonValue::from_string(builder.to_string());

    return run(schema_json);
}

JsonValue Parser::run(const String filename)
{
    auto schema_file = Core::File::construct(filename);
    if (!schema_file->open(Core::IODevice::ReadOnly)) {
        fprintf(stderr, "Couldn't open %s for reading: %s\n", filename.characters(), schema_file->error_string());
        return false;
    }
    JsonValue schema_json = JsonValue::from_string(schema_file->read_all());

    return run(schema_json);
}

JsonValue Parser::run(const JsonValue& json)
{
    if (json.is_bool()) {
        m_root_node = make<BooleanNode>(nullptr, "", json.as_bool());
        return JsonValue(true);
    }

    if (!json.is_object()) {
        add_parser_error("root json instance not of type object");
    }

    auto json_object = json.as_object();

    // FIXME: Here, we should load the file given in $schema, and check the $id in the root. This will provide the actual schema version used, that could be located anywhere.
    static String known_schema = "https://json-schema.org/draft/2019-09/schema";

    if (json_object.get("$schema").as_string_or(known_schema) != known_schema) {
        add_parser_error("unknown json schema provided, currently, only \"https://json-schema.org/draft/2019-09/schema\" is allowed for $schema.");
    }

    m_root_node = get_typed_node(json_object);
    if (!m_root_node.ptr()) {
        add_parser_error("root node could not be identified correctly");
    }

    if (m_parser_errors.size()) {
        JsonArray vals;
        for (auto& e : m_parser_errors) {
            vals.append(e);
        }
        return vals;
    }
    return JsonValue(true);
}

void Parser::add_parser_error(String error)
{
    m_parser_errors.append(error);
}

void Parser::parse_all_of(const JsonObject& json_object, JsonSchemaNode* node)
{

    if (json_object.has("allOf")) {
        auto allof = json_object.get_or("allOf", JsonArray());
        if (!allof.is_array()) {
            StringBuilder b;
            b.appendf("items value is not a json array, it is: %s", allof.to_string().characters());
            add_parser_error(b.build());
            return;
        }

        JsonArray allof_array = allof.as_array();
        for (auto& item : allof_array.values()) {
            OwnPtr<JsonSchemaNode> child_node = get_typed_node(item, node);
            if (child_node)
                node->append_all_of(child_node.release_nonnull());
        }
    }
}

OwnPtr<JsonSchemaNode> Parser::get_typed_node(const JsonValue& json_value, JsonSchemaNode* parent)
{
    OwnPtr<JsonSchemaNode> node;

    if (json_value.is_array()) {
        node = make<ArrayNode>(parent, "");
        ArrayNode& array_node = *static_cast<ArrayNode*>(node.ptr());

        for (auto& item : json_value.as_array().values()) {
            ASSERT(item.is_object());
            OwnPtr<JsonSchemaNode> child_node = get_typed_node(item, node.ptr());
            if (child_node)
                array_node.append_item(child_node.release_nonnull());
        }

    } else if (json_value.is_bool()) {
        node = make<BooleanNode>(parent, "", json_value.as_bool());

    } else if (json_value.is_null()) {
        node = make<NullNode>(parent, "");

    } else if (json_value.is_object()) {
        JsonObject json_object = json_value.as_object();
        JsonValue id = json_object.get("$id");
        JsonValue type = json_object.get("type");

        if (type.is_array()) {
            add_parser_error("multiple types for element not supported.");
        }
        auto type_str = type.as_string_or("");

        if (type_str == "null") {
            node = make<NullNode>(parent, "");

        } else if (type_str == "number" || type_str == "integer"
            || json_object.has("minimum")
            || json_object.has("maximum")
            || json_object.has("exclusiveMinimum")
            || json_object.has("exclusiveMaximum")
            || json_object.has("multipleOf")) {

            node = make<NumberNode>(parent, id.as_string_or(""));
            NumberNode& number_node = *static_cast<NumberNode*>(node.ptr());

            if (json_object.has("minimum")) {
                number_node.set_minimum(json_object.get("minimum").to_number<float>());
            }

            if (json_object.has("maximum")) {
                number_node.set_maximum(json_object.get("maximum").to_number<float>());
            }

        } else if (type_str == "array"
            || json_object.has("items")
            || json_object.has("additionalItems")
            || json_object.has("unevaluatedItems")
            || json_object.has("maxItems")
            || json_object.has("minItems")
            || json_object.has("uniqueItems")
            || json_object.has("maxContains")
            || json_object.has("minContains")) {

            node = make<ArrayNode>(parent, id.as_string_or(""));
            ArrayNode& array_node = *static_cast<ArrayNode*>(node.ptr());

            if (json_object.has("uniqueItems")) {
                array_node.set_unique_items(true);
            }

            if (json_object.has("additionalItems")) {
                array_node.set_additional_items(get_typed_node(json_object.get("additionalItems"), node.ptr()));
            }

            if (json_object.has("items")) {
                auto items = json_object.get_or("items", JsonObject());
                if (!items.is_object() && !items.is_array()) {
                    StringBuilder b;
                    b.appendf("items value is not a json object/array, it is: %s", items.to_string().characters());
                    add_parser_error(b.build());
                }

                if (items.is_object()) {
                    JsonObject items_object = items.as_object();
                    OwnPtr<JsonSchemaNode> child_node = get_typed_node(items_object, node.ptr());
                    if (child_node)
                        array_node.append_item(child_node.release_nonnull());
                } else if (items.is_array()) {
                    array_node.set_items_is_array(true);
                    JsonArray items_array = items.as_array();
                    for (auto& item : items_array.values()) {
                        ASSERT(item.is_object());
                        OwnPtr<JsonSchemaNode> child_node = get_typed_node(item, node.ptr());
                        if (child_node)
                            array_node.append_item(child_node.release_nonnull());
                    }
                }
            }

        } else if (type_str == "string"
            || json_object.has("maxLength")
            || json_object.has("minLength")
            || json_object.has("pattern")) {

            node = make<StringNode>(parent, id.as_string_or(""));

            auto pattern = json_object.get("pattern");
            if (!pattern.is_undefined()) {
                if (!pattern.is_string()) {
                    add_parser_error("pattern value is not a json string");
                } else {
                    static_cast<StringNode*>(node.ptr())->set_pattern(pattern.as_string());
                }
            }
            auto minLength = json_object.get("minLength");
            if (!minLength.is_undefined()) {
                if (!(minLength.is_u32() || minLength.is_u64())) {
                    add_parser_error("minLength value is not a non-negative integer");
                } else {
                    static_cast<StringNode*>(node.ptr())->set_min_length(minLength.to_u32());
                }
            }
            auto maxLength = json_object.get("maxLength");
            if (!maxLength.is_undefined()) {
                if (!(maxLength.is_u32() || maxLength.is_u64())) {
                    add_parser_error("maxLength value is not a non-negative integer");
                } else {
                    static_cast<StringNode*>(node.ptr())->set_max_length(maxLength.to_u32());
                }
            }
        } else {

            if (json_object.is_empty()) {
                node = make<BooleanNode>(parent, "", true);

            } else if (type_str == "object"
                || json_object.has("properties")
                || json_object.has("maxProperties")
                || json_object.has("required")
                || json_object.has("dependentRequired")) {

                node = make<ObjectNode>(parent, id.as_string_or(""));
                ObjectNode& obj_node = *static_cast<ObjectNode*>(node.ptr());

                auto properties = json_object.get_or("properties", JsonObject());
                if (!properties.is_object()) {
                    add_parser_error("properties value is not a json object");
                } else {

                    properties.as_object().for_each_member([&](auto& key, auto& json_value) {
                        if (!json_value.is_object()) {
                            add_parser_error("property element is not a json object");
                        } else {
                            OwnPtr<JsonSchemaNode> child_node = get_typed_node(json_value, node.ptr());
                            if (child_node)
                                obj_node.append_property(key, child_node.release_nonnull());
                        }
                    });
                }
                auto pattern_properties = json_object.get_or("patternProperties", JsonObject());
                if (!properties.is_object()) {
                    add_parser_error("patternProperties value is not a json object");
                } else {
                    if (pattern_properties.is_object()) {
                        pattern_properties.as_object().for_each_member([&](auto& key, auto& json_value) {
                            if (!json_value.is_object()) {
                                add_parser_error("patternProperty element is not a json object");
                            } else {
                                OwnPtr<JsonSchemaNode> child_node = get_typed_node(json_value, node.ptr());
                                if (child_node) {
                                    child_node->set_identified_by_pattern(true, key);
                                    obj_node.append_property(key, child_node.release_nonnull());
                                }
                            }
                        });
                    }
                }
                // FIXME: additionalProperties could be any valid json schema, not just true/false.
                auto additional_properties = json_object.get_or("additionalProperties", JsonValue(true));
                if (!additional_properties.is_bool()) {
                    add_parser_error("additionalProperties value is not a json bool");
                } else {
                    obj_node.set_additional_properties(additional_properties.as_bool());

                    auto required = json_object.get_or("required", JsonArray());
                    if (!required.is_array()) {
                        add_parser_error("required value is not a json array");
                    } else {
                        for (auto& required_property : required.as_array().values()) {
                            if (!required_property.is_string()) {
                                add_parser_error("required value is not string");
                                continue;
                            }
                            bool found = false;
                            for (auto& property : obj_node.properties()) {
                                if (property.key == required_property.as_string()) {
                                    found = true;
                                    property.value->set_required(true);
                                }
                            }
                            if (!found) {
                                StringBuilder b;
                                b.appendf("Specified required element '%s' not found in properties", required_property.as_string().characters());
                                add_parser_error(b.build());
                            }
                        }
                    }
                }
            }

            else {
                node = make<UndefinedNode>(parent);
            }
        }

        if (node) {
            parse_all_of(json_object, node);
        }
    }
    return move(node);
}
}
