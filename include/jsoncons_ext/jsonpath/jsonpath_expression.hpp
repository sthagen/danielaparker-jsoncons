// Copyright 2013-2024 Daniel Parker
// Distributed under the Boost license, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// See https://github.com/danielaparker/jsoncons for latest version

#ifndef JSONCONS_JSONPATH_JSONPATH_EXPR_HPP
#define JSONCONS_JSONPATH_JSONPATH_EXPR_HPP

#include <string>
#include <vector>
#include <memory>
#include <type_traits> // std::is_const
#include <limits> // std::numeric_limits
#include <utility> // std::move
#include <regex>
#include <algorithm> // std::reverse
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonpath/jsonpath_error.hpp>
#include <jsoncons_ext/jsonpath/expression.hpp>
#include <jsoncons_ext/jsonpath/json_location.hpp>
#include <jsoncons_ext/jsonpath/jsonpath_selector.hpp>

namespace jsoncons { 
namespace jsonpath {

    template <typename Json,typename TempAllocator =std::allocator<char>>
    class jsonpath_expression
    {
    public:
        using allocator_type = typename jsonpath_traits<Json>::allocator_type;
        using char_type = typename jsonpath_traits<Json>::char_type;
        using string_type = typename jsonpath_traits<Json>::string_type;
        using string_view_type = typename jsonpath_traits<Json>::string_view_type;
        
        using value_type = typename jsonpath_traits<Json>::value_type;
        using reference = typename jsonpath_traits<Json>::reference;
        using const_reference = typename jsonpath_traits<Json>::const_reference;
        
        using static_resources_type = jsoncons::jsonpath::detail::static_resources<value_type,reference>;
        using const_static_resources_type = jsoncons::jsonpath::detail::static_resources<value_type,const_reference>;
        using path_expression_type = jsoncons::jsonpath::detail::path_expression<value_type,reference>;
        using const_path_expression_type = jsoncons::jsonpath::detail::path_expression<value_type,const_reference>;
        using path_node_type = basic_path_node<typename Json::char_type>;
    private:
        allocator_type alloc_;
        std::unique_ptr<const_static_resources_type> const_static_resources_;
        const_path_expression_type const_expr_;
        std::unique_ptr<static_resources_type> static_resources_;
        path_expression_type expr_;
    public:
        jsonpath_expression(const allocator_set<allocator_type,TempAllocator>& alloc_set,
            std::unique_ptr<const_static_resources_type>&& const_resources,
            const_path_expression_type&& const_expr, 
            std::unique_ptr<static_resources_type>&& resources,
            path_expression_type&& expr)
            : alloc_(alloc_set.get_allocator()),
              const_static_resources_(std::move(const_resources)), 
              const_expr_(std::move(const_expr)),
              static_resources_(std::move(resources)), 
              expr_(std::move(expr))
        {
        }

        jsonpath_expression(const jsonpath_expression&) = delete;
        jsonpath_expression(jsonpath_expression&&) = default;

        jsonpath_expression& operator=(const jsonpath_expression&) = delete;
        jsonpath_expression& operator=(jsonpath_expression&&) = default;

        template <typename BinaryCallback>
        typename std::enable_if<extension_traits::is_binary_function_object<BinaryCallback,const string_type&,const_reference>::value,void>::type
        evaluate(const_reference root, BinaryCallback callback, result_options options = result_options()) const
        {
            jsoncons::jsonpath::detail::dynamic_resources<Json,const_reference> resources{alloc_};
            auto f = [&callback](const path_node_type& path, const_reference val)
            {
                callback(to_basic_string(path), val);
            };
            const_expr_.evaluate(resources, root, path_node_type{}, root, f, options | result_options::path);
        }

        value_type evaluate(const_reference root, result_options options = result_options()) const
        {
            if ((options & result_options::path) == result_options::path)
            {
                jsoncons::jsonpath::detail::dynamic_resources<value_type, const_reference> resources{ alloc_ };

                value_type result(json_array_arg, semantic_tag::none, alloc_);
                auto callback = [&result](const path_node_type& p, const_reference)
                {
                    result.emplace_back(to_basic_string(p));
                };
                const_expr_.evaluate(resources, root, path_node_type{}, root, callback, options);
                return result;
            }
            else
            {
                jsoncons::jsonpath::detail::dynamic_resources<value_type, const_reference> resources{ alloc_ };
                return const_expr_.evaluate(resources, root, path_node_type{}, root, options);
            }
        }

        value_type select(const_reference root, result_options options = result_options()) const
        {
            if ((options & result_options::path) == result_options::path)
            {
                jsoncons::jsonpath::detail::dynamic_resources<value_type, const_reference> resources{ alloc_ };

                value_type result(json_array_arg, semantic_tag::none, alloc_);
                auto callback = [&result](const path_node_type& p, const_reference)
                {
                    result.emplace_back(to_basic_string(p));
                };
                const_expr_.evaluate(resources, root, path_node_type{}, root, callback, options);
                return result;
            }
            else
            {
                jsoncons::jsonpath::detail::dynamic_resources<value_type, const_reference> resources{ alloc_ };
                return const_expr_.evaluate(resources, root, path_node_type{}, root, options);
            }
        }

        template <typename BinaryCallback>
        typename std::enable_if<extension_traits::is_binary_function_object<BinaryCallback,const path_node_type&,const_reference>::value,void>::type
        select(const_reference root, BinaryCallback callback, result_options options = result_options()) const
        {
            jsoncons::jsonpath::detail::dynamic_resources<value_type,const_reference> resources{alloc_};
            const_expr_.evaluate(resources, root, path_node_type{}, root, callback, options | result_options::path);
        }

        template <typename BinaryCallback>
        typename std::enable_if<extension_traits::is_binary_function_object<BinaryCallback,const path_node_type&,value_type&>::value,void>::type
        update(reference root, BinaryCallback callback) const
        {
            jsoncons::jsonpath::detail::dynamic_resources<value_type,reference> resources{alloc_};

            result_options options = result_options::nodups | result_options::path | result_options::sort_descending;
            expr_.evaluate(resources, root, path_node_type{}, root, callback, options);
        }

        std::vector<basic_json_location<char_type>> select_paths(const_reference root, 
            result_options options = result_options::nodups | result_options::sort) const
        {
            std::vector<basic_json_location<char_type>> result;

            options = options | result_options::path;

            auto callback = [&result](const path_node_type& path, const_reference)
            {
                result.emplace_back(path);
            };

            jsoncons::jsonpath::detail::dynamic_resources<value_type,const_reference> resources{alloc_};
            const_expr_.evaluate(resources, root, path_node_type{}, root, callback, options);

            return result;
        }
    };

    template <typename Json>
    jsonpath_expression<Json> make_expression(const typename Json::string_view_type& path,
        const jsoncons::jsonpath::custom_functions<typename jsonpath_traits<Json>::value_type>& funcs = jsoncons::jsonpath::custom_functions<typename jsonpath_traits<Json>::value_type>())
    {
        using value_type = typename jsonpath_traits<Json>::value_type;
        using reference = typename jsonpath_traits<Json>::reference;
        using const_reference = typename jsonpath_traits<Json>::const_reference;
        using static_resources_type = jsoncons::jsonpath::detail::static_resources<value_type,reference>;
        using const_static_resources_type = jsoncons::jsonpath::detail::static_resources<value_type,const_reference>;
        using evaluator_type = typename jsoncons::jsonpath::detail::jsonpath_evaluator<value_type, reference>;
        using const_evaluator_type = typename jsoncons::jsonpath::detail::jsonpath_evaluator<value_type, const_reference>;

        auto const_static_resources = jsoncons::make_unique<const_static_resources_type>(funcs);
        const_evaluator_type const_evaluator;
        auto const_expr = const_evaluator.compile(*const_static_resources, path);

        auto static_resources = jsoncons::make_unique<static_resources_type>(funcs);
        evaluator_type evaluator;
        auto expr = evaluator.compile(*static_resources, path);

        return jsonpath_expression<Json>(jsoncons::combine_allocators(), 
            std::move(const_static_resources), std::move(const_expr), 
            std::move(static_resources), std::move(expr));
    }

    template <typename Json>
    jsonpath_expression<Json> make_expression(const typename Json::string_view_type& expr, std::error_code& ec)
    {
        return make_expression<Json>(jsoncons::combine_allocators(), expr, custom_functions<Json>(), ec);
    }

    template <typename Json,typename TempAllocator >
    jsonpath_expression<Json> make_expression(const allocator_set<typename Json::allocator_type,TempAllocator>& alloc_set, 
        const typename Json::string_view_type& expr, std::error_code& ec)
    {
        return make_expression<Json>(alloc_set, expr, custom_functions<Json>(), ec);
    }

    template <typename Json,typename TempAllocator >
    jsonpath_expression<Json> make_expression(const allocator_set<typename Json::allocator_type,TempAllocator>& alloc_set, 
        const typename Json::string_view_type& path, 
        const custom_functions<Json>& funcs = custom_functions<Json>())
    {
        using value_type = typename jsonpath_traits<Json>::value_type;
        using reference = typename jsonpath_traits<Json>::reference;
        using const_reference = typename jsonpath_traits<Json>::const_reference;
        using static_resources_type = jsoncons::jsonpath::detail::static_resources<value_type,reference>;
        using const_static_resources_type = jsoncons::jsonpath::detail::static_resources<value_type,const_reference>;
        using evaluator_type = typename jsoncons::jsonpath::detail::jsonpath_evaluator<value_type, reference>;
        using const_evaluator_type = typename jsoncons::jsonpath::detail::jsonpath_evaluator<value_type, const_reference>;

        auto const_static_resources = jsoncons::make_unique<const_static_resources_type>(funcs,
            alloc_set.get_allocator());
        const_evaluator_type const_evaluator{alloc_set.get_allocator()};
        auto const_expr = const_evaluator.compile(*const_static_resources, path);

        auto static_resources = jsoncons::make_unique<static_resources_type>(funcs,
            alloc_set.get_allocator());
        evaluator_type evaluator{alloc_set.get_allocator()};
        auto expr = evaluator.compile(*static_resources, path);

        return jsonpath_expression<Json>(alloc_set, 
            std::move(const_static_resources), std::move(const_expr), 
            std::move(static_resources), std::move(expr));
    }

    template <typename Json,typename TempAllocator >
    jsonpath_expression<Json> make_expression(const allocator_set<typename Json::allocator_type,TempAllocator>& alloc_set,
        const typename Json::string_view_type& path,
        const jsoncons::jsonpath::custom_functions<typename jsonpath_traits<Json>::value_type>& funcs, std::error_code& ec)
    {
        using value_type = typename jsonpath_traits<Json>::value_type;
        using reference = typename jsonpath_traits<Json>::reference;
        using const_reference = typename jsonpath_traits<Json>::const_reference;
        using static_resources_type = jsoncons::jsonpath::detail::static_resources<value_type,reference>;
        using const_static_resources_type = jsoncons::jsonpath::detail::static_resources<value_type,const_reference>;
        using evaluator_type = typename jsoncons::jsonpath::detail::jsonpath_evaluator<value_type, reference>;
        using const_evaluator_type = typename jsoncons::jsonpath::detail::jsonpath_evaluator<value_type, const_reference>;

        auto const_static_resources = jsoncons::make_unique<const_static_resources_type>(funcs);
        const_evaluator_type const_evaluator{alloc_set.get_allocator()};
        auto const_expr = const_evaluator.compile(*const_static_resources, path, ec);

        auto static_resources = jsoncons::make_unique<static_resources_type>(funcs);
        evaluator_type evaluator{alloc_set.get_allocator()};
        auto expr = evaluator.compile(*static_resources, path, ec);

        return jsonpath_expression<Json>(alloc_set, 
            std::move(const_static_resources), std::move(const_expr), 
            std::move(static_resources), std::move(expr));
    }

    template <typename Json>
    std::size_t remove(Json& root, const jsoncons::basic_string_view<typename Json::char_type>& path_string)
    {
        std::size_t count = 0;

        auto expr = jsonpath::make_expression<json>(path_string);
        std::vector<jsonpath::json_location> locations = expr.select_paths(root,
            jsonpath::result_options::nodups | jsonpath::result_options::sort_descending);

        for (const auto& location : locations)
        {
            std::size_t n = jsonpath::remove(root, location);
            count += n;
        }
        return count;
    }

} // namespace jsonpath
} // namespace jsoncons

#endif
