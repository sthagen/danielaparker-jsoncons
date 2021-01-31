// Copyright 2021 Daniel Parker
// Distributed under the Boost license, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// See https://github.com/danielaparker/jsoncons for latest version

#ifndef JSONCONS_JSONPATH_JSON_QUERY_HPP
#define JSONCONS_JSONPATH_JSON_QUERY_HPP

#include <string>
#include <vector>
#include <memory>
#include <type_traits> // std::is_const
#include <limits> // std::numeric_limits
#include <utility> // std::move
#include <regex>
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonpath/jsonpath_error.hpp>
#include <jsoncons_ext/jsonpath/path_expression.hpp>

namespace jsoncons { namespace jsonpath {

    // token

    struct slice
    {
        jsoncons::optional<int64_t> start_;
        jsoncons::optional<int64_t> stop_;
        int64_t step_;

        slice()
            : start_(), stop_(), step_(1)
        {
        }

        slice(const jsoncons::optional<int64_t>& start, const jsoncons::optional<int64_t>& end, int64_t step) 
            : start_(start), stop_(end), step_(step)
        {
        }

        slice(const slice& other)
            : start_(other.start_), stop_(other.stop_), step_(other.step_)
        {
        }

        slice& operator=(const slice& rhs) 
        {
            if (this != &rhs)
            {
                if (rhs.start_)
                {
                    start_ = rhs.start_;
                }
                else
                {
                    start_.reset();
                }
                if (rhs.stop_)
                {
                    stop_ = rhs.stop_;
                }
                else
                {
                    stop_.reset();
                }
                step_ = rhs.step_;
            }
            return *this;
        }

        int64_t get_start(std::size_t size) const
        {
            if (start_)
            {
                auto len = *start_ >= 0 ? *start_ : (static_cast<int64_t>(size) + *start_);
                return len <= static_cast<int64_t>(size) ? len : static_cast<int64_t>(size);
            }
            else
            {
                if (step_ >= 0)
                {
                    return 0;
                }
                else 
                {
                    return static_cast<int64_t>(size);
                }
            }
        }

        int64_t get_stop(std::size_t size) const
        {
            if (stop_)
            {
                auto len = *stop_ >= 0 ? *stop_ : (static_cast<int64_t>(size) + *stop_);
                return len <= static_cast<int64_t>(size) ? len : static_cast<int64_t>(size);
            }
            else
            {
                return step_ >= 0 ? static_cast<int64_t>(size) : -1;
            }
        }

        int64_t step() const
        {
            return step_; // Allow negative
        }
    };

    namespace detail {
     
    enum class path_state 
    {
        start,
        expect_function_expr,
        expression_lhs,
        expression_rhs,
        filter_expression,
        filter_expression_rhs,
        recursive_descent_or_expression_lhs,
        path_or_literal_or_function,
        json_text_or_function,
        json_text_or_function_name,
        json_text_string,
        json_value,
        json_string,
        identifier_or_function_expr,
        name_or_left_bracket,
        unquoted_string,
        number,
        function_expression,
        argument,
        identifier,
        single_quoted_string,
        double_quoted_string,
        bracketed_unquoted_name_or_union,
        union_expression,
        single_quoted_name_or_union,
        double_quoted_name_or_union,
        identifier_or_union,
        bracket_specifier_or_union,
        bracketed_wildcard,
        index_or_slice,
        wildcard_or_union,
        union_element,
        index_or_slice_or_union,
        index,
        integer,
        digit,
        slice_expression_stop,
        slice_expression_step,
        comma_or_right_bracket,
        expect_right_bracket,
        quoted_string_escape_char,
        escape_u1, 
        escape_u2, 
        escape_u3, 
        escape_u4, 
        escape_expect_surrogate_pair1, 
        escape_expect_surrogate_pair2, 
        escape_u5, 
        escape_u6, 
        escape_u7, 
        escape_u8,
        expression,
        comparator_expression,
        eq_or_regex,
        expect_regex,
        regex,
        cmp_lt_or_lte,
        cmp_gt_or_gte,
        cmp_ne,
        expect_or,
        expect_and
    };

    JSONCONS_STRING_LITERAL(length_literal, 'l', 'e', 'n', 'g', 't', 'h')

    template<class Json,
             class JsonReference>
    class jsonpath_evaluator : public ser_context
    {
    public:
        using char_type = typename Json::char_type;
        using string_type = std::basic_string<char_type,std::char_traits<char_type>>;
        using string_view_type = typename Json::string_view_type;
        using path_node_type = path_node<Json,JsonReference>;
        using value_type = Json;
        using reference = JsonReference;
        using pointer = typename path_node_type::pointer;
        using selector_base_type = selector_base<Json,JsonReference>;
        using token_type = token<Json,JsonReference>;
        using path_expression_type = path_expression<Json,JsonReference>;
        using function_base_type = function_base<Json,JsonReference>;

    private:

        // path_selector
        class path_selector : public selector_base_type
        {
            std::unique_ptr<selector_base_type> tail_selector_;
        public:
            using selector_base_type::generate_path;

            path_selector()
                : selector_base_type(true, 11), tail_selector_()
            {
            }

            void append_selector(std::unique_ptr<selector_base_type>&& expr) override
            {
                if (!tail_selector_)
                {
                    tail_selector_ = std::move(expr);
                }
                else
                {
                    tail_selector_->append_selector(std::move(expr));
                }
            }

            void evaluate_tail(dynamic_resources<Json,JsonReference>& resources,
                               const string_type& path, 
                               reference root,
                               reference val,
                               std::vector<path_node_type>& nodes,
                               node_type& ndtype,
                               result_flags flags) const
            {
                if (!tail_selector_)
                {
                    nodes.emplace_back(path, std::addressof(val));
                }
                else
                {
                    tail_selector_->select(resources, path, root, val, nodes, ndtype, flags);
                }
            }

            virtual std::string to_string(int = 0) const override
            {
                return tail_selector_ ? std::string() : tail_selector_->to_string();
            }
        };

        class identifier_selector final : public path_selector
        {
            string_type identifier_;
        public:
            using path_selector::generate_path;

            identifier_selector(const string_view_type& identifier)
                : path_selector(), identifier_(identifier)
            {
            }

            void select(dynamic_resources<Json,JsonReference>& resources,
                        const string_type& path, 
                        reference root,
                        reference val,
                        std::vector<path_node_type>& nodes,
                        node_type& ndtype,
                        result_flags flags) const override
            {
                //std::cout << "identifier selector val: " << val << ", identifier: " << identifier_  << "\n";

                ndtype = node_type::single;
                if (val.is_object())
                {
                    auto it = val.find(identifier_);
                    if (it != val.object_range().end())
                    {
                        this->evaluate_tail(resources, generate_path(path, identifier_, flags), 
                                                root, it->value(), nodes, ndtype, flags);
                    }
                }
                else if (val.is_array())
                {
                    auto r = jsoncons::detail::to_integer_decimal<int64_t>(identifier_.data(), identifier_.size());
                    if (r)
                    {
                        std::size_t index = (r.value() >= 0) ? static_cast<std::size_t>(r.value()) : static_cast<std::size_t>(static_cast<int64_t>(val.size()) + r.value());
                        if (index < val.size())
                        {
                            this->evaluate_tail(resources, generate_path(path, index, flags), 
                                                root, val[index], nodes, ndtype, flags);
                        }
                    }
                    else if (identifier_ == length_literal<char_type>() && val.size() > 0)
                    {
                        pointer ptr = resources.create_json(val.size());
                        this->evaluate_tail(resources, generate_path(path, identifier_, flags), 
                                                root, *ptr, nodes, ndtype, flags);
                    }
                }
                else if (val.is_string() && identifier_ == length_literal<char_type>())
                {
                    string_view_type sv = val.as_string_view();
                    std::size_t count = unicons::u32_length(sv.begin(), sv.end());
                    pointer ptr = resources.create_json(count);
                    this->evaluate_tail(resources, generate_path(path, identifier_, flags), 
                                            root, *ptr, nodes, ndtype, flags);
                }
                //std::cout << "end identifier_selector\n";
            }

            std::string to_string(int level = 0) const override
            {
                std::string s;
                if (level > 0)
                {
                    s.append("\n");
                    s.append(level*2, ' ');
                }
                s.append("identifier: ");
                //s.append(identifier_);

                return s;
            }
        };

        class root_selector final : public path_selector
        {
            std::size_t id_;
        public:
            using path_selector::generate_path;

            root_selector(std::size_t id)
                : path_selector(), id_(id)
            {
            }

            void select(dynamic_resources<Json,JsonReference>& resources,
                        const string_type& path, 
                        reference root,
                        reference,
                        std::vector<path_node_type>& nodes,
                        node_type& ndtype,
                        result_flags flags) const override
            {
                if (resources.is_cached(id_))
                {
                    resources.retrieve_from_cache(id_, nodes, ndtype);
                }
                else
                {
                    std::vector<path_node_type> v;
                    this->evaluate_tail(resources, path, 
                                        root, root, v, ndtype, flags);
                    resources.add_to_cache(id_, v, ndtype);
                    for (auto&& item : v)
                    {
                        nodes.push_back(std::move(item));
                    }
                }
            }

            std::string to_string(int level = 0) const override
            {
                std::string s;
                if (level > 0)
                {
                    s.append("\n");
                    s.append(level*2, ' ');
                }
                s.append("root_selector");

                return s;
            }
        };

        class current_node_selector final : public path_selector
        {
        public:
            using path_selector::generate_path;

            current_node_selector()
            {
            }

            void select(dynamic_resources<Json,JsonReference>& resources,
                        const string_type& path, 
                        reference root,
                        reference current,
                        std::vector<path_node_type>& nodes,
                        node_type& ndtype,
                        result_flags flags) const override
            {
                ndtype = node_type::single;
                this->evaluate_tail(resources, path, 
                                    root, current, nodes, ndtype, flags);
            }

            std::string to_string(int level = 0) const override
            {
                std::string s;
                if (level > 0)
                {
                    s.append("\n");
                    s.append(level*2, ' ');
                }
                s.append("current_node_selector");

                return s;
            }
        };

        class index_selector final : public path_selector
        {
            int64_t index_;
        public:
            using path_selector::generate_path;

            index_selector(int64_t index)
                : path_selector(), index_(index)
            {
            }

            void select(dynamic_resources<Json,JsonReference>& resources,
                        const string_type& path, 
                        reference root,
                        reference val,
                        std::vector<path_node_type>& nodes,
                        node_type& ndtype,
                        result_flags flags) const override
            {
                ndtype = node_type::single;
                if (val.is_array())
                {
                    int64_t slen = static_cast<int64_t>(val.size());
                    if (index_ >= 0 && index_ < slen)
                    {
                        std::size_t index = static_cast<std::size_t>(index_);
                        //std::cout << "path: " << path << ", val: " << val << ", index: " << index << "\n";
                        //nodes.emplace_back(generate_path(path, index, flags),std::addressof(val.at(index)));
                        //nodes.emplace_back(path, std::addressof(val));
                        this->evaluate_tail(resources, generate_path(path, index, flags), 
                                                root, val.at(index), nodes, ndtype, flags);
                    }
                    else if ((slen + index_) >= 0 && (slen+index_) < slen)
                    {
                        std::size_t index = static_cast<std::size_t>(slen + index_);
                        //std::cout << "path: " << path << ", val: " << val << ", index: " << index << "\n";
                        //nodes.emplace_back(generate_path(path, index ,flags),std::addressof(val.at(index)));
                        this->evaluate_tail(resources, generate_path(path, index, flags), 
                                                root, val.at(index), nodes, ndtype, flags);
                    }
                }
            }
        };

        class wildcard_selector final : public path_selector
        {
        public:
            using path_selector::generate_path;

            wildcard_selector()
                : path_selector()
            {
            }

            void select(dynamic_resources<Json,JsonReference>& resources,
                        const string_type& path, 
                        reference root,
                        reference val,
                        std::vector<path_node_type>& nodes,
                        node_type& ndtype,
                        result_flags flags) const override
            {
                //std::cout << "wildcard_selector: " << val << "\n";
                ndtype = node_type::multi; // always multi

                node_type tmptype;
                if (val.is_array())
                {
                    for (std::size_t i = 0; i < val.size(); ++i)
                    {
                        this->evaluate_tail(resources, generate_path(path, i, flags), root, val[i], nodes, tmptype, flags);
                    }
                }
                else if (val.is_object())
                {
                    for (auto& item : val.object_range())
                    {
                        this->evaluate_tail(resources, generate_path(path, item.key(), flags), root, item.value(), nodes, tmptype, flags);
                    }
                }
                //std::cout << "end wildcard_selector\n";
            }

            std::string to_string(int level = 0) const override
            {
                std::string s;
                if (level > 0)
                {
                    s.append("\n");
                    s.append(level*2, ' ');
                }
                s.append("wildcard\n");
                s.append(path_selector::to_string(level));

                return s;
            }
        };

        class recursive_selector final : public path_selector
        {
        public:
            using path_selector::generate_path;

            recursive_selector()
                : path_selector()
            {
            }

            void select(dynamic_resources<Json,JsonReference>& resources,
                        const string_type& path, 
                        reference root,
                        reference val,
                        std::vector<path_node_type>& nodes,
                        node_type& ndtype,
                        result_flags flags) const override
            {
                //std::cout << "wildcard_selector: " << val << "\n";
                if (val.is_array())
                {
                    this->evaluate_tail(resources, path, root, val, nodes, ndtype, flags);
                    for (std::size_t i = 0; i < val.size(); ++i)
                    {
                        select(resources, generate_path(path, i, flags), root, val[i], nodes, ndtype, flags);
                    }
                }
                else if (val.is_object())
                {
                    this->evaluate_tail(resources, path, root, val, nodes, ndtype, flags);
                    for (auto& item : val.object_range())
                    {
                        select(resources, generate_path(path, item.key(), flags), root, item.value(), nodes, ndtype, flags);
                    }
                }
                //std::cout << "end wildcard_selector\n";
            }

            std::string to_string(int level = 0) const override
            {
                std::string s;
                if (level > 0)
                {
                    s.append("\n");
                    s.append(level*2, ' ');
                }
                s.append("wildcard\n");
                s.append(path_selector::to_string(level));

                return s;
            }
        };

        class union_selector final : public path_selector
        {
            std::vector<path_expression_type> expressions_;

        public:
            using path_selector::generate_path;

            union_selector(std::vector<path_expression_type>&& expressions)
                : path_selector(), expressions_(std::move(expressions))
            {
            }

            void select(dynamic_resources<Json,JsonReference>& resources,
                        const string_type& path, 
                        reference root,
                        reference val, 
                        std::vector<path_node_type>& nodes,
                        node_type& ndtype,
                        result_flags flags) const override
            {
                //std::cout << "union_selector select val: " << val << "\n";
                ndtype = node_type::multi;

                auto callback = [&](path_node_type& node)
                {
                    //std::cout << "union select callback: node: " << *node.ptr << "\n";
                    this->evaluate_tail(resources, node.path, root, *node.ptr, nodes, ndtype, flags);
                };
                for (auto& expr : expressions_)
                {
                    expr.evaluate(resources, path, root, val, callback, flags);
                }
            }

            std::string to_string(int level = 0) const override
            {
                std::string s;
                if (level > 0)
                {
                    s.append("\n");
                    s.append(level * 2, ' ');
                }
                s.append("union selector\n");
                //s.append(expr_.to_string(level));

                return s;
            }
        };

        static bool is_false(const std::vector<path_node_type>& nodes)
        {
            if (nodes.size() != 1)
            {
                return nodes.empty();
            }
            auto valp = nodes.front().ptr; 
            return ((valp->is_array() && valp->empty()) ||
                     (valp->is_object() && valp->empty()) ||
                     (valp->is_string() && valp->as_string_view().empty()) ||
                     (valp->is_bool() && !valp->as_bool()) ||
                     (valp->is_number() && *valp == Json(0)) ||
                     valp->is_null());
        }

        static bool is_true(const std::vector<path_node_type>& nodes)
        {
            return !is_false(nodes);
        }

        class filter_selector final : public path_selector
        {
            path_expression_type expr_;

        public:
            using path_selector::generate_path;

            filter_selector(path_expression_type&& expr)
                : path_selector(), expr_(std::move(expr))
            {
            }

            void select(dynamic_resources<Json,JsonReference>& resources,
                        const string_type& path, 
                        reference root,
                        reference val, 
                        std::vector<path_node_type>& nodes,
                        node_type& ndtype,
                        result_flags flags) const override
            {
                if (val.is_array())
                {
                    for (std::size_t i = 0; i < val.size(); ++i)
                    {
                        std::vector<path_node_type> temp;
                        auto callback = [&temp](path_node_type& node)
                        {
                            temp.push_back(node);
                        };
                        expr_.evaluate(resources, generate_path(path, i, flags), root, val[i], callback, flags);
                        if (is_true(temp))
                        {
                            this->evaluate_tail(resources, generate_path(path,i,flags), root, val[i], nodes, ndtype, flags);
                        }
                    }
                }
                else if (val.is_object())
                {
                    for (auto& member : val.object_range())
                    {
                        std::vector<path_node_type> temp;
                        auto callback = [&temp](path_node_type& node)
                        {
                            temp.push_back(node);
                        };
                        expr_.evaluate(resources, generate_path(path, member.key(), flags), root, member.value(), callback, flags);
                        if (is_true(temp))
                        {
                            this->evaluate_tail(resources, generate_path(path,member.key(),flags), root, member.value(), nodes, ndtype, flags);
                        }
                    }
                }
            }

            std::string to_string(int level = 0) const override
            {
                std::string s;
                if (level > 0)
                {
                    s.append("\n");
                    s.append(level * 2, ' ');
                }
                s.append("filter selector\n");
                //s.append(expr_.to_string(level));

                return s;
            }
        };

        class expression_selector final : public path_selector
        {
            path_expression_type expr_;

        public:
            using path_selector::generate_path;

            expression_selector(path_expression_type&& expr)
                : path_selector(), expr_(std::move(expr))
            {
            }

            void select(dynamic_resources<Json,JsonReference>& resources,
                        const string_type& path, 
                        reference root,
                        reference val, 
                        std::vector<path_node_type>& nodes,
                        node_type& ndtype,
                        result_flags flags) const override
            {
                std::vector<path_node_type> temp;
                auto callback = [&temp](path_node_type& node)
                {
                    temp.push_back(node);
                };
                expr_.evaluate(resources, path, root, val, callback, flags);
                if (!temp.empty())
                {
                    auto& j = *temp.back().ptr;
                    if (j.template is<std::size_t>() && val.is_array())
                    {
                        std::size_t start = j.template as<std::size_t>();
                        this->evaluate_tail(resources, generate_path(path, start, flags), root, val.at(start), nodes, ndtype, flags);
                    }
                    else if (j.is_string() && val.is_object())
                    {
                        this->evaluate_tail(resources, generate_path(path, j.as_string(), flags), root, val.at(j.as_string_view()), nodes, ndtype, flags);
                    }
                }
            }

            std::string to_string(int level = 0) const override
            {
                std::string s;
                if (level > 0)
                {
                    s.append("\n");
                    s.append(level * 2, ' ');
                }
                s.append("expression selector\n");
                //s.append(expr_.to_string(level));

                return s;
            }
        };

        class slice_selector final : public path_selector
        {
            slice slice_;
        public:
            using path_selector::generate_path;

            slice_selector(const slice& slic)
                : path_selector(), slice_(slic) 
            {
            }

            void select(dynamic_resources<Json,JsonReference>& resources,
                        const string_type& path, 
                        reference root,
                        reference val,
                        std::vector<path_node_type>& nodes,
                        node_type& ndtype,
                        result_flags flags) const override
            {
                ndtype = node_type::multi;

                if (val.is_array())
                {
                    auto start = slice_.get_start(val.size());
                    auto end = slice_.get_stop(val.size());
                    auto step = slice_.step();

                    if (step > 0)
                    {
                        if (start < 0)
                        {
                            start = 0;
                        }
                        if (end > static_cast<int64_t>(val.size()))
                        {
                            end = val.size();
                        }
                        for (int64_t i = start; i < end; i += step)
                        {
                            std::size_t j = static_cast<std::size_t>(i);
                            this->evaluate_tail(resources, generate_path(path, j, flags), root, val[j], nodes, ndtype, flags);
                        }
                    }
                    else if (step < 0)
                    {
                        if (start >= static_cast<int64_t>(val.size()))
                        {
                            start = static_cast<int64_t>(val.size()) - 1;
                        }
                        if (end < -1)
                        {
                            end = -1;
                        }
                        for (int64_t i = start; i > end; i += step)
                        {
                            std::size_t j = static_cast<std::size_t>(i);
                            if (j < val.size())
                            {
                                this->evaluate_tail(resources, generate_path(path,j,flags), root, val[j], nodes, ndtype, flags);
                            }
                        }
                    }
                }
            }
        };

        class function_expression final : public selector_base_type
        {
        public:
            path_expression_type expr_;

            function_expression(path_expression_type&& expr)
                : selector_base_type(false,0), expr_(std::move(expr))
            {
            }

            void select(dynamic_resources<Json,JsonReference>& resources,
                        const string_type& path, 
                        reference root,
                        reference val, 
                        std::vector<path_node_type>& nodes,
                        node_type& ndtype,
                        result_flags flags) const override
            {
                ndtype = node_type::single;

                //std::cout << "function val: " << val << "\n";
                auto callback = [&nodes](path_node_type& node)
                {
                    nodes.push_back(node);
                };
                return expr_.evaluate(resources, path, root, val, callback, flags);
            }

            std::string to_string(int level = 0) const override
            {
                std::string s;
                if (level > 0)
                {
                    s.append("\n");
                    s.append(level*2, ' ');
                }
                s.append("function expression");

                return s;
            }
        };

        std::size_t line_;
        std::size_t column_;
        const char_type* begin_input_;
        const char_type* end_input_;
        const char_type* p_;

        using argument_type = std::vector<pointer>;
        std::vector<argument_type> function_stack_;
        std::vector<path_state> state_stack_;
        std::vector<token_type> output_stack_;
        std::vector<token_type> operator_stack_;

    public:
        jsonpath_evaluator()
            : line_(1), column_(1),
              begin_input_(nullptr), end_input_(nullptr),
              p_(nullptr)
        {
        }

        jsonpath_evaluator(std::size_t line, std::size_t column)
            : line_(line), column_(column),
              begin_input_(nullptr), end_input_(nullptr),
              p_(nullptr)
        {
        }

        std::size_t line() const
        {
            return line_;
        }

        std::size_t column() const
        {
            return column_;
        }

        path_expression_type compile(static_resources<value_type,reference>& resources, const string_view_type& path)
        {
            std::error_code ec;
            auto result = compile(resources, path.data(), path.length(), ec);
            if (ec)
            {
                JSONCONS_THROW(jsonpath_error(ec, line_, column_));
            }
            return result;
        }

        path_expression_type compile(static_resources<value_type,reference>& resources, const string_view_type& path, std::error_code& ec)
        {
            JSONCONS_TRY
            {
                return compile(resources, path.data(), path.length(), ec);
            }
            JSONCONS_CATCH(...)
            {
                ec = jsonpath_errc::unidentified_error;
            }
        }

        path_expression_type compile(static_resources<value_type,reference>& resources,
                                     const char_type* path, 
                                     std::size_t length)
        {
            std::error_code ec;
            auto result = compile(resources, path, length, ec);
            if (ec)
            {
                JSONCONS_THROW(jsonpath_error(ec, line_, column_));
            }
            return result;
        }

        path_expression_type compile(static_resources<value_type,reference>& resources,
                                     const char_type* path, 
                                     std::size_t length,
                                     std::error_code& ec)
        {
            std::size_t selector_id = 0;

            string_type function_name;
            string_type buffer;
            int64_t json_text_level = 0;
            uint32_t cp = 0;
            uint32_t cp2 = 0;

            begin_input_ = path;
            end_input_ = path + length;
            p_ = begin_input_;

            string_type s = {'$'};

            slice slic;
            int paren_level = 0;

            state_stack_.emplace_back(path_state::start);
            while (p_ < end_input_ && !state_stack_.empty())
            {
                switch (state_stack_.back())
                {
                    case path_state::start: 
                    {
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '$':
                            {
                                push_token(root_node_arg, ec);
                                state_stack_.emplace_back(path_state::expression_rhs);
                                ++p_;
                                ++column_;
                                break;
                            }
                            default:
                            {
                                state_stack_.emplace_back(path_state::expression_rhs);
                                state_stack_.emplace_back(path_state::expect_function_expr);
                                state_stack_.emplace_back(path_state::unquoted_string);
                                break;
                            }
                        }
                        break;
                    }
                    case path_state::recursive_descent_or_expression_lhs:
                        switch (*p_)
                        {
                            case '.':
                                push_token(token_type(jsoncons::make_unique<recursive_selector>()), ec);
                                ++p_;
                                ++column_;
                                state_stack_.back() = path_state::name_or_left_bracket;
                                break;
                            default:
                                state_stack_.back() = path_state::expression_lhs;
                                break;
                        }
                        break;
                    case path_state::name_or_left_bracket: 
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '[': // [ can follow ..
                                state_stack_.back() = path_state::bracket_specifier_or_union;
                                ++p_;
                                ++column_;
                                break;
                            default:
                                buffer.clear();
                                state_stack_.back() = path_state::expression_lhs;
                                break;
                        }
                        break;
                    case path_state::json_string:
                    {
                        //std::cout << "literal: " << buffer << "\n";
                        push_token(token_type(literal_arg, Json(buffer)), ec);
                        buffer.clear();
                        state_stack_.pop_back(); // json_value
                        ++p_;
                        ++column_;
                        break;
                    }
                    case path_state::path_or_literal_or_function: 
                    {
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '$':
                                state_stack_.back() = path_state::expression_lhs;                                
                                break;
                            case '@':
                                state_stack_.back() = path_state::expression_lhs;                                
                                break;
                            case '(':
                            {
                                ++p_;
                                ++column_;
                                ++paren_level;
                                push_token(lparen_arg, ec);
                                break;
                            }
                            case '\'':
                                state_stack_.back() = path_state::json_string;
                                state_stack_.emplace_back(path_state::single_quoted_string);
                                ++p_;
                                ++column_;
                                break;
                            case '\"':
                                state_stack_.back() = path_state::json_string;
                                state_stack_.emplace_back(path_state::double_quoted_string);
                                ++p_;
                                ++column_;
                                break;
                            case '!':
                            {
                                ++p_;
                                ++column_;
                                push_token(token_type(resources.get_unary_not()), ec);
                                break;
                            }
                            case '-':
                            {
                                ++p_;
                                ++column_;
                                push_token(token_type(resources.get_unary_minus()), ec);
                                break;
                            }
                            // number
                            case '0':case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':
                            {
                                state_stack_.back() = path_state::json_value;
                                state_stack_.emplace_back(path_state::number);
                                break;
                            }
                            default:
                            {
                                state_stack_.back() = path_state::json_text_or_function;
                                state_stack_.emplace_back(path_state::json_text_or_function_name);
                                json_text_level = 0;
                                break;
                            }
                        }
                        break;
                    }
                    case path_state::json_text_or_function:
                    {
                        switch(*p_)
                        {
                            case '(':
                            {
                                auto f = resources.get_function(buffer, ec);
                                if (ec)
                                {
                                    return path_expression_type();
                                }
                                buffer.clear();
                                ++paren_level;
                                push_token(current_node_arg, ec);
                                push_token(token_type(begin_function_arg), ec);
                                push_token(token_type(f), ec);
                                state_stack_.back() = path_state::function_expression;
                                state_stack_.emplace_back(path_state::argument);
                                state_stack_.emplace_back(path_state::expression_rhs);
                                state_stack_.emplace_back(path_state::path_or_literal_or_function);
                                ++p_;
                                ++column_;
                                break;
                            }
                            default:
                            {
                                json_decoder<Json> decoder;
                                basic_json_parser<char_type> parser;
                                parser.update(buffer.data(),buffer.size());
                                parser.parse_some(decoder, ec);
                                if (ec)
                                {
                                    return path_expression_type();
                                }
                                parser.finish_parse(decoder, ec);
                                if (ec)
                                {
                                    return path_expression_type();
                                }
                                push_token(token_type(literal_arg, decoder.get_result()), ec);
                                buffer.clear();
                                state_stack_.pop_back();
                                break;
                            }
                        }
                        break;
                    }
                    case path_state::json_value:
                    {
                        json_decoder<Json> decoder;
                        basic_json_parser<char_type> parser;
                        parser.update(buffer.data(),buffer.size());
                        parser.parse_some(decoder, ec);
                        if (ec)
                        {
                            return path_expression_type();
                        }
                        parser.finish_parse(decoder, ec);
                        if (ec)
                        {
                            return path_expression_type();
                        }
                        push_token(token_type(literal_arg, decoder.get_result()), ec);
                        buffer.clear();
                        state_stack_.pop_back();
                        break;
                    }
                    case path_state::json_text_or_function_name:
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '{':
                            case '[':
                                ++json_text_level;
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                            case '}':
                            case ']':
                                --json_text_level;
                                if (json_text_level == 0)
                                {
                                    state_stack_.pop_back(); 
                                }
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                            case '-':case '0':case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':
                                if (json_text_level == 0)
                                {
                                    state_stack_.back() = path_state::number;
                                }
                                else
                                {
                                    state_stack_.emplace_back(path_state::number);
                                }
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                            case '\"':
                                if (json_text_level == 0)
                                {
                                    state_stack_.back() = path_state::json_text_string;
                                }
                                else
                                {
                                    state_stack_.emplace_back(path_state::json_text_string);
                                }
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                            case ':':
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                            default:
                                if (json_text_level == 0)
                                {
                                    state_stack_.back() = path_state::unquoted_string;
                                }
                                else
                                {
                                    state_stack_.emplace_back(path_state::unquoted_string);
                                }
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                        };
                        break;
                    case path_state::number: 
                        switch (*p_)
                        {
                            case '-':case '0':case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':
                            case 'e':case 'E':case '.':
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                            default:
                                state_stack_.pop_back(); // number
                                break;
                        };
                        break;
                    case path_state::json_text_string: 
                        switch (*p_)
                        {
                            case '\\':
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                if (p_ == end_input_)
                                {
                                    ec = jsonpath_errc::unexpected_eof;
                                    return path_expression_type();
                                }
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                            case '\"':
                                buffer.push_back(*p_);
                                state_stack_.pop_back(); 
                                ++p_;
                                ++column_;
                                break;
                            default:
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                        };
                        break;
                    case path_state::expression_lhs: 
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '*':
                                push_token(token_type(jsoncons::make_unique<wildcard_selector>()), ec);
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            case '\'':
                                state_stack_.back() = path_state::identifier;
                                state_stack_.emplace_back(path_state::single_quoted_string);
                                ++p_;
                                ++column_;
                                break;
                            case '\"':
                                state_stack_.back() = path_state::identifier;
                                state_stack_.emplace_back(path_state::double_quoted_string);
                                ++p_;
                                ++column_;
                                break;
                            case '[':
                                state_stack_.emplace_back(path_state::bracket_specifier_or_union);
                                ++p_;
                                ++column_;
                                break;
                            case '$':
                                ++p_;
                                ++column_;
                                push_token(token_type(root_node_arg), ec);
                                push_token(token_type(jsoncons::make_unique<root_selector>(selector_id++)), ec);
                                state_stack_.pop_back();
                                break;
                            case '@':
                                ++p_;
                                ++column_;
                                push_token(token_type(current_node_arg), ec); // ISSUE
                                push_token(token_type(jsoncons::make_unique<current_node_selector>()), ec);
                                state_stack_.pop_back();
                                break;
                            case '.':
                                ec = jsonpath_errc::expected_key;
                                return path_expression_type();
                            default:
                                buffer.clear();
                                state_stack_.back() = path_state::identifier_or_function_expr;
                                state_stack_.emplace_back(path_state::unquoted_string);
                                break;
                        }
                        break;
                    case path_state::identifier_or_function_expr:
                    {
                        switch(*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '(':
                            {
                                auto f = resources.get_function(buffer, ec);
                                if (ec)
                                {
                                    return path_expression_type();
                                }
                                buffer.clear();
                                ++paren_level;
                                push_token(current_node_arg, ec);
                                push_token(token_type(begin_function_arg), ec);
                                push_token(token_type(f), ec);
                                state_stack_.back() = path_state::function_expression;
                                state_stack_.emplace_back(path_state::argument);
                                state_stack_.emplace_back(path_state::expression_rhs);
                                state_stack_.emplace_back(path_state::expression_lhs);
                                ++p_;
                                ++column_;
                                break;
                            }
                            default:
                            {
                                push_token(token_type(jsoncons::make_unique<identifier_selector>(buffer)), ec);
                                buffer.clear();
                                state_stack_.pop_back(); 
                                break;
                            }
                        }
                        break;
                    }
                    case path_state::expect_function_expr:
                    {
                        switch(*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '(':
                            {
                                auto f = resources.get_function(buffer, ec);
                                if (ec)
                                {
                                    return path_expression_type();
                                }
                                buffer.clear();
                                ++paren_level;
                                push_token(current_node_arg, ec);
                                push_token(token_type(begin_function_arg), ec);
                                push_token(token_type(f), ec);
                                state_stack_.back() = path_state::function_expression;
                                state_stack_.emplace_back(path_state::argument);
                                state_stack_.emplace_back(path_state::expression_rhs);
                                state_stack_.emplace_back(path_state::path_or_literal_or_function);
                                ++p_;
                                ++column_;
                                break;
                            }
                            default:
                            {
                                ec = jsonpath_errc::expected_root_or_function;
                                return path_expression_type();
                            }
                        }
                        break;
                    }
                    case path_state::function_expression:
                    {
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case ',':
                                push_token(token_type(current_node_arg), ec);
                                state_stack_.emplace_back(path_state::argument);
                                state_stack_.emplace_back(path_state::expression_rhs);
                                state_stack_.emplace_back(path_state::path_or_literal_or_function);
                                ++p_;
                                ++column_;
                                break;
                            case ')':
                            {
                                --paren_level;
                                push_token(token_type(end_function_arg), ec);
                                state_stack_.pop_back(); 
                                ++p_;
                                ++column_;
                                break;
                            }
                            default:
                                break;
                        }
                        break;
                    }
                    case path_state::argument:
                    {
                        push_token(argument_arg, ec);
                        state_stack_.pop_back();
                        break;
                    }
                    case path_state::unquoted_string: 
                        switch (*p_)
                        {
                            case '(':
                            case ')':
                            case ']':
                            case '[':
                            case '.':
                            case ',':
                            case ' ':case '\t':
                            case '\r':
                            case '\n':
                            case '!':
                            case '=':
                            case '<':
                            case '>':
                            case '~':
                            case '|':
                            case '&':
                            case '+':
                            case '*':
                            case '/':
                            case '@':
                            case '$':
                            case '?':
                                state_stack_.pop_back(); // unquoted_string
                                break;
                            default:
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                        };
                        break;                    
                    case path_state::expression_rhs: 
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '.':
                                state_stack_.emplace_back(path_state::recursive_descent_or_expression_lhs);
                                ++p_;
                                ++column_;
                                break;
                            case '[':
                                state_stack_.emplace_back(path_state::bracket_specifier_or_union);
                                ++p_;
                                ++column_;
                                break;
                            case ')':
                            {
                                if (state_stack_.size() > 1 && (*(state_stack_.rbegin()+1) == path_state::argument))
                                {
                                    state_stack_.pop_back();
                                }
                                else
                                {
                                    ++p_;
                                    ++column_;
                                    --paren_level;
                                    push_token(rparen_arg, ec);
                                }
                                break;
                            }
                            case ']':
                            case ',':
                                state_stack_.pop_back();
                                break;
                            default:
                                ec = jsonpath_errc::expected_separator;
                                return path_expression_type();
                        };
                        break;
                    case path_state::filter_expression_rhs: 
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '.':
                                state_stack_.emplace_back(path_state::recursive_descent_or_expression_lhs);
                                ++p_;
                                ++column_;
                                break;
                            case '[':
                                state_stack_.emplace_back(path_state::bracket_specifier_or_union);
                                ++p_;
                                ++column_;
                                break;
                            case ')':
                            {
                                if (state_stack_.size() > 1 && (*(state_stack_.rbegin()+1) == path_state::argument))
                                {
                                    state_stack_.pop_back();
                                }
                                else
                                {
                                    ++p_;
                                    ++column_;
                                    --paren_level;
                                    push_token(rparen_arg, ec);
                                }
                                break;
                            }
                            case '|':
                                ++p_;
                                ++column_;
                                state_stack_.emplace_back(path_state::path_or_literal_or_function);
                                state_stack_.emplace_back(path_state::expect_or);
                                break;
                            case '&':
                                ++p_;
                                ++column_;
                                state_stack_.emplace_back(path_state::path_or_literal_or_function);
                                state_stack_.emplace_back(path_state::expect_and);
                                break;
                            case '<':
                            case '>':
                            {
                                state_stack_.emplace_back(path_state::comparator_expression);
                                break;
                            }
                            case '=':
                            {
                                state_stack_.emplace_back(path_state::eq_or_regex);
                                ++p_;
                                ++column_;
                                break;
                            }
                            case '!':
                            {
                                ++p_;
                                ++column_;
                                state_stack_.emplace_back(path_state::path_or_literal_or_function);
                                state_stack_.emplace_back(path_state::cmp_ne);
                                break;
                            }
                            case '+':
                                state_stack_.emplace_back(path_state::path_or_literal_or_function);
                                push_token(token_type(resources.get_plus_operator()), ec);
                                ++p_;
                                ++column_;
                                break;
                            case '-':
                                state_stack_.emplace_back(path_state::path_or_literal_or_function);
                                push_token(token_type(resources.get_minus_operator()), ec);
                                ++p_;
                                ++column_;
                                break;
                            case '*':
                                state_stack_.emplace_back(path_state::path_or_literal_or_function);
                                push_token(token_type(resources.get_mult_operator()), ec);
                                ++p_;
                                ++column_;
                                break;
                            case '/':
                                state_stack_.emplace_back(path_state::path_or_literal_or_function);
                                push_token(token_type(resources.get_div_operator()), ec);
                                ++p_;
                                ++column_;
                                break;
                            case ']':
                            case ',':
                                state_stack_.pop_back();
                                break;
                            default:
                                ec = jsonpath_errc::expected_separator;
                                return path_expression_type();
                        };
                        break;
                    case path_state::expect_or:
                    {
                        switch (*p_)
                        {
                            case '|':
                                push_token(token_type(resources.get_or_operator()), ec);
                                state_stack_.pop_back(); 
                                ++p_;
                                ++column_;
                                break;
                            default:
                                ec = jsonpath_errc::expected_or;
                                return path_expression_type();
                        }
                        break;
                    }
                    case path_state::expect_and:
                    {
                        switch(*p_)
                        {
                            case '&':
                                push_token(token_type(resources.get_and_operator()), ec);
                                state_stack_.pop_back(); // expect_and
                                ++p_;
                                ++column_;
                                break;
                            default:
                                ec = jsonpath_errc::expected_and;
                                return path_expression_type();
                        }
                        break;
                    }
                    case path_state::comparator_expression:
                        switch(*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '<':
                                ++p_;
                                ++column_;
                                state_stack_.back() = path_state::path_or_literal_or_function;
                                state_stack_.emplace_back(path_state::cmp_lt_or_lte);
                                break;
                            case '>':
                                ++p_;
                                ++column_;
                                state_stack_.back() = path_state::path_or_literal_or_function;
                                state_stack_.emplace_back(path_state::cmp_gt_or_gte);
                                break;
                            default:
                                if (state_stack_.size() > 1)
                                {
                                    state_stack_.pop_back();
                                }
                                else
                                {
                                    ec = jsonpath_errc::syntax_error;
                                    return path_expression_type();
                                }
                                break;
                        }
                        break;
                    case path_state::eq_or_regex:
                        switch(*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '=':
                            {
                                push_token(token_type(resources.get_eq_operator()), ec);
                                state_stack_.back() = path_state::path_or_literal_or_function;
                                ++p_;
                                ++column_;
                                break;
                            }
                            case '~':
                            {
                                ++p_;
                                ++column_;
                                state_stack_.emplace_back(path_state::expect_regex);
                                break;
                            }
                            default:
                                if (state_stack_.size() > 1)
                                {
                                    state_stack_.pop_back();
                                }
                                else
                                {
                                    ec = jsonpath_errc::syntax_error;
                                    return path_expression_type();
                                }
                                break;
                        }
                        break;
                    case path_state::expect_regex: 
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '/':
                                state_stack_.back() = path_state::regex;
                                ++p_;
                                ++column_;
                                break;
                            default: 
                                ec = jsonpath_errc::expected_forward_slash;
                                return path_expression_type();
                        };
                        break;
                    case path_state::regex: 
                    {
                        switch (*p_)
                        {                   
                            case '/':
                                {
                                    std::regex::flag_type flags = std::regex_constants::ECMAScript; 
                                    if (p_+1  < end_input_ && *(p_+1) == 'i')
                                    {
                                        ++p_;
                                        ++column_;
                                        flags |= std::regex_constants::icase;
                                    }
                                    std::basic_regex<char_type> pattern(buffer, flags);
                                    push_token(resources.get_regex_operator(std::move(pattern)), ec);
                                    buffer.clear();
                                }
                                state_stack_.pop_back();
                                break;

                            default: 
                                buffer.push_back(*p_);
                                break;
                        }
                        ++p_;
                        ++column_;
                        break;
                    }
                    case path_state::cmp_lt_or_lte:
                    {
                        switch(*p_)
                        {
                            case '=':
                                push_token(token_type(resources.get_lte_operator()), ec);
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            default:
                                push_token(token_type(resources.get_lt_operator()), ec);
                                state_stack_.pop_back();
                                break;
                        }
                        break;
                    }
                    case path_state::cmp_gt_or_gte:
                    {
                        switch(*p_)
                        {
                            case '=':
                                push_token(token_type(resources.get_gte_operator()), ec);
                                state_stack_.pop_back(); 
                                ++p_;
                                ++column_;
                                break;
                            default:
                                push_token(token_type(resources.get_gt_operator()), ec);
                                state_stack_.pop_back(); 
                                break;
                        }
                        break;
                    }
                    case path_state::cmp_ne:
                    {
                        switch(*p_)
                        {
                            case '=':
                                push_token(token_type(resources.get_ne_operator()), ec);
                                state_stack_.pop_back(); 
                                ++p_;
                                ++column_;
                                break;
                            default:
                                ec = jsonpath_errc::expected_comparator;
                                return path_expression_type();
                        }
                        break;
                    }
                    case path_state::identifier:
                        switch (*p_)
                        {
                            case '\'':
                            case '\"':
                                ++p_;
                                ++column_;
                                push_token(token_type(jsoncons::make_unique<identifier_selector>(buffer)), ec);
                                buffer.clear();
                                state_stack_.pop_back(); 
                                break;
                            default:
                                push_token(token_type(jsoncons::make_unique<identifier_selector>(buffer)), ec);
                                buffer.clear();
                                state_stack_.pop_back(); 
                                break;
                        }
                        break;
                    case path_state::single_quoted_string:
                        switch (*p_)
                        {
                            case '\'':
                                state_stack_.pop_back();
                                break;
                            case '\\':
                                state_stack_.emplace_back(path_state::quoted_string_escape_char);
                                ++p_;
                                ++column_;
                                break;
                            default:
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                        };
                        break;
                    case path_state::double_quoted_string: 
                        switch (*p_)
                        {
                            case '\"':
                                state_stack_.pop_back();
                                break;
                            case '\\':
                                state_stack_.emplace_back(path_state::quoted_string_escape_char);
                                ++p_;
                                ++column_;
                                break;
                            default:
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                        };
                        break;
                    case path_state::comma_or_right_bracket:
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case ',':
                                state_stack_.back() = path_state::bracket_specifier_or_union;
                                ++p_;
                                ++column_;
                                break;
                            case ']':
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            default:
                                ec = jsonpath_errc::expected_right_bracket;
                                return path_expression_type();
                        }
                        break;
                    case path_state::expect_right_bracket:
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case ']':
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            default:
                                ec = jsonpath_errc::expected_right_bracket;
                                return path_expression_type();
                        }
                        break;
                    case path_state::bracket_specifier_or_union:
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '(':
                            {
                                push_token(token_type(begin_expression_arg), ec);
                                state_stack_.back() = path_state::expression;
                                state_stack_.emplace_back(path_state::filter_expression_rhs);
                                state_stack_.emplace_back(path_state::path_or_literal_or_function);
                                ++paren_level;
                                push_token(lparen_arg, ec);
                                ++p_;
                                ++column_;
                                break;
                            }
                            case '?':
                            {
                                push_token(token_type(begin_union_arg), ec);
                                state_stack_.back() = path_state::union_expression; // union
                                push_token(token_type(begin_filter_arg), ec);
                                state_stack_.emplace_back(path_state::filter_expression);
                                state_stack_.emplace_back(path_state::filter_expression_rhs);
                                state_stack_.emplace_back(path_state::path_or_literal_or_function);
                                ++p_;
                                ++column_;
                                break;
                            }
                            case '*':
                                state_stack_.back() = path_state::wildcard_or_union;
                                ++p_;
                                ++column_;
                                break;
                            case '\'':
                                state_stack_.back() = path_state::single_quoted_name_or_union;
                                ++p_;
                                ++column_;
                                break;
                            case '\"':
                                state_stack_.back() = path_state::double_quoted_name_or_union;
                                ++p_;
                                ++column_;
                                break;
                            case ':': // slice_expression
                                state_stack_.back() = path_state::index_or_slice_or_union;
                                break;
                            case '-':case '0':case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':
                                state_stack_.back() = path_state::index_or_slice_or_union;
                                state_stack_.emplace_back(path_state::integer);
                                break;
                            case '$':
                                push_token(token_type(begin_union_arg), ec);
                                push_token(root_node_arg, ec);
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::expression_lhs);                                
                                state_stack_.emplace_back(path_state::expression_rhs);                                
                                ++p_;
                                ++column_;
                                break;
                            case '@':
                                push_token(token_type(begin_union_arg), ec);
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::expression_lhs);                                
                                break;
                            default:
                                buffer.clear();
                                buffer.push_back(*p_);
                                state_stack_.back() = path_state::bracketed_unquoted_name_or_union;
                                ++p_;
                                ++column_;
                                break;
                        }
                        break;
                    case path_state::union_element:
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case ':': // slice_expression
                                state_stack_.back() = path_state::index_or_slice;
                                break;
                            case '-':case '0':case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':
                                state_stack_.back() = path_state::index_or_slice;
                                state_stack_.emplace_back(path_state::integer);
                                break;
                            case '?':
                            {
                                push_token(token_type(begin_filter_arg), ec);
                                state_stack_.back() = path_state::filter_expression;
                                state_stack_.emplace_back(path_state::filter_expression_rhs);
                                state_stack_.emplace_back(path_state::path_or_literal_or_function);
                                ++p_;
                                ++column_;
                                break;
                            }
                            default:
                                state_stack_.back() = path_state::expression_lhs;
                                break;
                        }
                        break;

                    case path_state::integer:
                        switch(*p_)
                        {
                            case '-':
                                buffer.push_back(*p_);
                                state_stack_.back() = path_state::digit;
                                ++p_;
                                ++column_;
                                break;
                            default:
                                state_stack_.back() = path_state::digit;
                                break;
                        }
                        break;
                    case path_state::digit:
                        switch(*p_)
                        {
                            case '0':case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                            default:
                                state_stack_.pop_back(); // digit
                                break;
                        }
                        break;
                    case path_state::index_or_slice_or_union:
                        switch(*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case ']':
                            {
                                if (buffer.empty())
                                {
                                    ec = jsonpath_errc::invalid_number;
                                    return path_expression_type();
                                }
                                auto r = jsoncons::detail::to_integer<int64_t>(buffer.data(), buffer.size());
                                if (!r)
                                {
                                    ec = jsonpath_errc::invalid_number;
                                    return path_expression_type();
                                }
                                push_token(token_type(jsoncons::make_unique<index_selector>(r.value())), ec);
                                buffer.clear();
                                state_stack_.pop_back(); // index_or_slice_or_union
                                ++p_;
                                ++column_;
                                break;
                            }
                            case ',':
                            {
                                push_token(token_type(begin_union_arg), ec);
                                if (buffer.empty())
                                {
                                    ec = jsonpath_errc::invalid_number;
                                    return path_expression_type();
                                }
                                else
                                {
                                    auto r = jsoncons::detail::to_integer<int64_t>(buffer.data(), buffer.size());
                                    if (!r)
                                    {
                                        ec = jsonpath_errc::invalid_number;
                                        return path_expression_type();
                                    }
                                    push_token(token_type(jsoncons::make_unique<index_selector>(r.value())), ec);

                                    buffer.clear();
                                }
                                push_token(token_type(separator_arg), ec);
                                buffer.clear();
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::union_element);
                                ++p_;
                                ++column_;
                                break;
                            }
                            case ':':
                            {
                                if (!buffer.empty())
                                {
                                    auto r = jsoncons::detail::to_integer<int64_t>(buffer.data(), buffer.size());
                                    if (!r)
                                    {
                                        ec = jsonpath_errc::invalid_number;
                                        return path_expression_type();
                                    }
                                    slic.start_ = r.value();
                                    buffer.clear();
                                }
                                push_token(token_type(begin_union_arg), ec);
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::slice_expression_stop);
                                state_stack_.emplace_back(path_state::integer);
                                ++p_;
                                ++column_;
                                break;
                            }
                            default:
                                ec = jsonpath_errc::expected_right_bracket;
                                return path_expression_type();
                        }
                        break;
                    case path_state::index:
                        switch(*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case ']':
                            case '.':
                            case ',':
                            {
                                if (buffer.empty())
                                {
                                    ec = jsonpath_errc::invalid_number;
                                    return path_expression_type();
                                }
                                else
                                {
                                    auto r = jsoncons::detail::to_integer<int64_t>(buffer.data(), buffer.size());
                                    if (!r)
                                    {
                                        ec = jsonpath_errc::invalid_number;
                                        return path_expression_type();
                                    }
                                    push_token(token_type(jsoncons::make_unique<index_selector>(r.value())), ec);

                                    buffer.clear();
                                }
                                state_stack_.pop_back(); // index
                                break;
                            }
                            default:
                                ec = jsonpath_errc::expected_right_bracket;
                                return path_expression_type();
                        }
                        break;
                    case path_state::slice_expression_stop:
                    {
                        if (!buffer.empty())
                        {
                            auto r = jsoncons::detail::to_integer<int64_t>(buffer.data(), buffer.size());
                            if (!r)
                            {
                                ec = jsonpath_errc::invalid_number;
                                return path_expression_type();
                            }
                            slic.stop_ = jsoncons::optional<int64_t>(r.value());
                            buffer.clear();
                        }
                        switch(*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case ']':
                            case ',':
                                push_token(token_type(jsoncons::make_unique<slice_selector>(slic)), ec);
                                slic = slice{};
                                state_stack_.pop_back(); // bracket_specifier2
                                break;
                            case ':':
                                state_stack_.back() = path_state::slice_expression_step;
                                state_stack_.emplace_back(path_state::integer);
                                ++p_;
                                ++column_;
                                break;
                            default:
                                ec = jsonpath_errc::expected_right_bracket;
                                return path_expression_type();
                        }
                        break;
                    }
                    case path_state::slice_expression_step:
                    {
                        if (!buffer.empty())
                        {
                            auto r = jsoncons::detail::to_integer<int64_t>(buffer.data(), buffer.size());
                            if (!r)
                            {
                                ec = jsonpath_errc::invalid_number;
                                return path_expression_type();
                            }
                            if (r.value() == 0)
                            {
                                ec = jsonpath_errc::step_cannot_be_zero;
                                return path_expression_type();
                            }
                            slic.step_ = r.value();
                            buffer.clear();
                        }
                        switch(*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case ']':
                            case ',':
                                push_token(token_type(jsoncons::make_unique<slice_selector>(slic)), ec);
                                buffer.clear();
                                slic = slice{};
                                state_stack_.pop_back(); // slice_expression_step
                                break;
                            default:
                                ec = jsonpath_errc::expected_right_bracket;
                                return path_expression_type();
                        }
                        break;
                    }

                    case path_state::bracketed_unquoted_name_or_union:
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case ']': 
                                push_token(token_type(jsoncons::make_unique<identifier_selector>(buffer)), ec);
                                buffer.clear();
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            case '.':
                                push_token(token_type(begin_union_arg), ec);
                                push_token(token_type(jsoncons::make_unique<identifier_selector>(buffer)), ec);
                                buffer.clear();
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::expression_lhs);                                
                                ++p_;
                                ++column_;
                                break;
                            case '[':
                                push_token(token_type(begin_union_arg), ec);
                                push_token(token_type(jsoncons::make_unique<identifier_selector>(buffer)), ec);
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::expression_lhs);                                
                                ++p_;
                                ++column_;
                                break;
                            case ',': 
                                push_token(token_type(begin_union_arg), ec);
                                push_token(token_type(jsoncons::make_unique<identifier_selector>(buffer)), ec);
                                push_token(token_type(separator_arg), ec);
                                buffer.clear();
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::expression_lhs);                                
                                ++p_;
                                ++column_;
                                break;
                            default:
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                        }
                        break;
                    case path_state::union_expression:
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '.':
                                state_stack_.emplace_back(path_state::expression_lhs);
                                ++p_;
                                ++column_;
                                break;
                            case '[':
                                state_stack_.emplace_back(path_state::bracket_specifier_or_union);
                                ++p_;
                                ++column_;
                                break;
                            case ',': 
                                push_token(token_type(separator_arg), ec);
                                //state_stack_.emplace_back(path_state::expression_lhs);
                                state_stack_.emplace_back(path_state::union_element);
                                ++p_;
                                ++column_;
                                break;
                            case ']': 
                                push_token(token_type(end_union_arg), ec);
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            default:
                                ec = jsonpath_errc::expected_right_bracket;
                                return path_expression_type();
                        }
                        break;
                    case path_state::identifier_or_union:
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case ']': 
                                push_token(token_type(jsoncons::make_unique<identifier_selector>(buffer)), ec);
                                buffer.clear();
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            case '.':
                                push_token(token_type(begin_union_arg), ec);
                                push_token(token_type(jsoncons::make_unique<identifier_selector>(buffer)), ec);
                                buffer.clear();
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::expression_lhs);                                
                                ++p_;
                                ++column_;
                                break;
                            case '[':
                                push_token(token_type(begin_union_arg), ec);
                                push_token(token_type(jsoncons::make_unique<identifier_selector>(buffer)), ec);
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::expression_lhs);                                
                                ++p_;
                                ++column_;
                                break;
                            case ',': 
                                push_token(token_type(begin_union_arg), ec);
                                push_token(token_type(jsoncons::make_unique<identifier_selector>(buffer)), ec);
                                push_token(token_type(separator_arg), ec);
                                buffer.clear();
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::expression_lhs);                                
                                ++p_;
                                ++column_;
                                break;
                            default:
                                ec = jsonpath_errc::expected_right_bracket;
                                return path_expression_type();
                        }
                        break;
                    case path_state::bracketed_wildcard:
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '[':
                            case ']':
                            case ',':
                            case '.':
                                push_token(token_type(jsoncons::make_unique<wildcard_selector>()), ec);
                                buffer.clear();
                                state_stack_.pop_back();
                                break;
                            default:
                                ec = jsonpath_errc::expected_right_bracket;
                                return path_expression_type();
                        }
                        break;
                    case path_state::index_or_slice:
                        switch(*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case ',':
                            case ']':
                            {
                                if (buffer.empty())
                                {
                                    ec = jsonpath_errc::invalid_number;
                                    return path_expression_type();
                                }
                                else
                                {
                                    auto r = jsoncons::detail::to_integer<int64_t>(buffer.data(), buffer.size());
                                    if (!r)
                                    {
                                        ec = jsonpath_errc::invalid_number;
                                        return path_expression_type();
                                    }
                                    push_token(token_type(jsoncons::make_unique<index_selector>(r.value())), ec);

                                    buffer.clear();
                                }
                                state_stack_.pop_back(); // bracket_specifier
                                break;
                            }
                            case ':':
                            {
                                if (!buffer.empty())
                                {
                                    auto r = jsoncons::detail::to_integer<int64_t>(buffer.data(), buffer.size());
                                    if (!r)
                                    {
                                        ec = jsonpath_errc::invalid_number;
                                        return path_expression_type();
                                    }
                                    slic.start_ = r.value();
                                    buffer.clear();
                                }
                                state_stack_.back() = path_state::slice_expression_stop;
                                state_stack_.emplace_back(path_state::integer);
                                ++p_;
                                ++column_;
                                break;
                            }
                            default:
                                ec = jsonpath_errc::expected_right_bracket;
                                return path_expression_type();
                        }
                        break;
                    case path_state::wildcard_or_union:
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case ']': 
                                push_token(token_type(jsoncons::make_unique<wildcard_selector>()), ec);
                                buffer.clear();
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            case '.':
                                push_token(token_type(begin_union_arg), ec);
                                push_token(token_type(jsoncons::make_unique<wildcard_selector>()), ec);
                                buffer.clear();
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::expression_lhs);                                
                                ++p_;
                                ++column_;
                                break;
                            case '[':
                                push_token(token_type(begin_union_arg), ec);
                                push_token(token_type(jsoncons::make_unique<wildcard_selector>()), ec);
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::expression_lhs);                                
                                ++p_;
                                ++column_;
                                break;
                            case ',': 
                                push_token(token_type(begin_union_arg), ec);
                                push_token(token_type(jsoncons::make_unique<wildcard_selector>()), ec);
                                push_token(token_type(separator_arg), ec);
                                buffer.clear();
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::expression_lhs);                                
                                ++p_;
                                ++column_;
                                break;
                            default:
                                ec = jsonpath_errc::expected_right_bracket;
                                return path_expression_type();
                        }
                        break;
                    case path_state::single_quoted_name_or_union:
                        switch (*p_)
                        {
                            case '\'':
                                state_stack_.back() = path_state::identifier_or_union;
                                break;
                            case '\\':
                                state_stack_.emplace_back(path_state::quoted_string_escape_char);
                                break;
                            default:
                                buffer.push_back(*p_);
                                break;
                        };
                        ++p_;
                        ++column_;
                        break;
                    case path_state::double_quoted_name_or_union: 
                        switch (*p_)
                        {
                            case '\"':
                                state_stack_.back() = path_state::identifier_or_union;
                                break;
                            case '\\':
                                state_stack_.emplace_back(path_state::quoted_string_escape_char);
                                break;
                            default:
                                buffer.push_back(*p_);
                                break;
                        };
                        ++p_;
                        ++column_;
                        break;
                    case path_state::quoted_string_escape_char:
                        switch (*p_)
                        {
                            case '\"':
                                buffer.push_back('\"');
                                ++p_;
                                ++column_;
                                state_stack_.pop_back();
                                break;
                            case '\'':
                                buffer.push_back('\'');
                                ++p_;
                                ++column_;
                                state_stack_.pop_back();
                                break;
                            case '\\': 
                                buffer.push_back('\\');
                                ++p_;
                                ++column_;
                                state_stack_.pop_back();
                                break;
                            case '/':
                                buffer.push_back('/');
                                ++p_;
                                ++column_;
                                state_stack_.pop_back();
                                break;
                            case 'b':
                                buffer.push_back('\b');
                                ++p_;
                                ++column_;
                                state_stack_.pop_back();
                                break;
                            case 'f':
                                buffer.push_back('\f');
                                ++p_;
                                ++column_;
                                state_stack_.pop_back();
                                break;
                            case 'n':
                                buffer.push_back('\n');
                                ++p_;
                                ++column_;
                                state_stack_.pop_back();
                                break;
                            case 'r':
                                buffer.push_back('\r');
                                ++p_;
                                ++column_;
                                state_stack_.pop_back();
                                break;
                            case 't':
                                buffer.push_back('\t');
                                ++p_;
                                ++column_;
                                state_stack_.pop_back();
                                break;
                            case 'u':
                                ++p_;
                                ++column_;
                                state_stack_.back() = path_state::escape_u1;
                                break;
                            default:
                                ec = jsonpath_errc::illegal_escaped_character;
                                return path_expression_type();
                        }
                        break;
                    case path_state::escape_u1:
                        cp = append_to_codepoint(0, *p_, ec);
                        if (ec)
                        {
                            return path_expression_type();
                        }
                        ++p_;
                        ++column_;
                        state_stack_.back() = path_state::escape_u2;
                        break;
                    case path_state::escape_u2:
                        cp = append_to_codepoint(cp, *p_, ec);
                        if (ec)
                        {
                            return path_expression_type();
                        }
                        ++p_;
                        ++column_;
                        state_stack_.back() = path_state::escape_u3;
                        break;
                    case path_state::escape_u3:
                        cp = append_to_codepoint(cp, *p_, ec);
                        if (ec)
                        {
                            return path_expression_type();
                        }
                        ++p_;
                        ++column_;
                        state_stack_.back() = path_state::escape_u4;
                        break;
                    case path_state::escape_u4:
                        cp = append_to_codepoint(cp, *p_, ec);
                        if (ec)
                        {
                            return path_expression_type();
                        }
                        if (unicons::is_high_surrogate(cp))
                        {
                            ++p_;
                            ++column_;
                            state_stack_.back() = path_state::escape_expect_surrogate_pair1;
                        }
                        else
                        {
                            unicons::convert(&cp, &cp + 1, std::back_inserter(buffer));
                            ++p_;
                            ++column_;
                            state_stack_.pop_back();
                        }
                        break;
                    case path_state::escape_expect_surrogate_pair1:
                        switch (*p_)
                        {
                            case '\\': 
                                ++p_;
                                ++column_;
                                state_stack_.back() = path_state::escape_expect_surrogate_pair2;
                                break;
                            default:
                                ec = jsonpath_errc::invalid_codepoint;
                                return path_expression_type();
                        }
                        break;
                    case path_state::escape_expect_surrogate_pair2:
                        switch (*p_)
                        {
                            case 'u': 
                                ++p_;
                                ++column_;
                                state_stack_.back() = path_state::escape_u5;
                                break;
                            default:
                                ec = jsonpath_errc::invalid_codepoint;
                                return path_expression_type();
                        }
                        break;
                    case path_state::escape_u5:
                        cp2 = append_to_codepoint(0, *p_, ec);
                        if (ec)
                        {
                            return path_expression_type();
                        }
                        ++p_;
                        ++column_;
                        state_stack_.back() = path_state::escape_u6;
                        break;
                    case path_state::escape_u6:
                        cp2 = append_to_codepoint(cp2, *p_, ec);
                        if (ec)
                        {
                            return path_expression_type();
                        }
                        ++p_;
                        ++column_;
                        state_stack_.back() = path_state::escape_u7;
                        break;
                    case path_state::escape_u7:
                        cp2 = append_to_codepoint(cp2, *p_, ec);
                        if (ec)
                        {
                            return path_expression_type();
                        }
                        ++p_;
                        ++column_;
                        state_stack_.back() = path_state::escape_u8;
                        break;
                    case path_state::escape_u8:
                    {
                        cp2 = append_to_codepoint(cp2, *p_, ec);
                        if (ec)
                        {
                            return path_expression_type();
                        }
                        uint32_t codepoint = 0x10000 + ((cp & 0x3FF) << 10) + (cp2 & 0x3FF);
                        unicons::convert(&codepoint, &codepoint + 1, std::back_inserter(buffer));
                        state_stack_.pop_back();
                        ++p_;
                        ++column_;
                        break;
                    }
                    case path_state::filter_expression:
                    {
                        switch(*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case ',':
                            case ']':
                            {
                                push_token(token_type(end_filter_arg), ec);
                                state_stack_.pop_back();
                                break;
                            }
                            default:
                                ec = jsonpath_errc::expected_right_bracket;
                                return path_expression_type();
                        }
                        break;
                    }
                    case path_state::expression:
                    {
                        switch(*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case ']':
                            {
                                push_token(token_type(end_expression_arg), ec);
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            }
                            default:
                                ec = jsonpath_errc::expected_right_bracket;
                                return path_expression_type();
                        }
                        break;
                    }
                    default:
                        ++p_;
                        ++column_;
                        break;
                }
            }

            if (state_stack_.empty())
            {
                ec = jsonpath_errc::syntax_error;
                return path_expression_type();
            }
            if (state_stack_.back() == path_state::start)
            {
                ec = jsonpath_errc::unexpected_eof;
                return path_expression_type();
            }

            if (state_stack_.size() >= 3)
            {
                if (state_stack_.back() == path_state::unquoted_string)
                {
                    push_token(token_type(jsoncons::make_unique<identifier_selector>(buffer)), ec);
                    state_stack_.pop_back(); // unquoted_string
                    buffer.clear();
                    if (state_stack_.back() == path_state::identifier_or_function_expr)
                    {
                        state_stack_.pop_back(); // identifier
                    }
                }
                else if (state_stack_.back() == path_state::digit)
                {
                    if (buffer.empty())
                    {
                        ec = jsonpath_errc::invalid_number;
                        return path_expression_type();
                    }
                    auto r = jsoncons::detail::to_integer<int64_t>(buffer.data(), buffer.size());
                    if (!r)
                    {
                        ec = jsonpath_errc::invalid_number;
                        return path_expression_type();
                    }
                    push_token(token_type(jsoncons::make_unique<index_selector>(r.value())), ec);
                    buffer.clear();
                    state_stack_.pop_back(); // index_or_slice_or_union
                    if (state_stack_.back() == path_state::index)
                    {
                        state_stack_.pop_back(); // index
                    }
                }
            }

            if (state_stack_.size() > 2)
            {
                ec = jsonpath_errc::unexpected_eof;
                return path_expression_type();
            }

            return path_expression_type(std::move(output_stack_));
        }

        void advance_past_space_character()
        {
            switch (*p_)
            {
                case ' ':case '\t':
                    ++p_;
                    ++column_;
                    break;
                case '\r':
                    if (p_+1 < end_input_ && *(p_+1) == '\n')
                        ++p_;
                    ++line_;
                    column_ = 1;
                    ++p_;
                    break;
                case '\n':
                    ++line_;
                    column_ = 1;
                    ++p_;
                    break;
                default:
                    break;
            }
        }

        void unwind_rparen(std::error_code& ec)
        {
            auto it = operator_stack_.rbegin();
            while (it != operator_stack_.rend() && !it->is_lparen())
            {
                output_stack_.emplace_back(std::move(*it));
                ++it;
            }
            if (it == operator_stack_.rend())
            {
                ec = jsonpath_errc::unbalanced_parenthesis;
                return;
            }
            ++it;
            operator_stack_.erase(it.base(),operator_stack_.end());
        }

        void push_token(token_type&& tok, std::error_code& ec)
        {
            switch (tok.type())
            {
                case token_kind::begin_filter:
                    output_stack_.emplace_back(std::move(tok));
                    operator_stack_.emplace_back(token_type(lparen_arg));
                    break;
                case token_kind::end_filter:
                {
                    unwind_rparen(ec);
                    if (ec)
                    {
                        return;
                    }
                    std::vector<token_type> toks;
                    auto it = output_stack_.rbegin();
                    while (it != output_stack_.rend() && it->type() != token_kind::begin_filter)
                    {
                        toks.insert(toks.begin(), std::move(*it));
                        ++it;
                    }
                    if (it == output_stack_.rend())
                    {
                        JSONCONS_THROW(json_runtime_error<std::runtime_error>("Unbalanced braces"));
                    }
                    ++it;
                    output_stack_.erase(it.base(),output_stack_.end());

                    if (!output_stack_.empty() && output_stack_.back().is_path())
                    {
                        output_stack_.back().selector_->append_selector(jsoncons::make_unique<filter_selector>(path_expression_type(std::move(toks))));
                    }
                    else
                    {
                        output_stack_.emplace_back(token_type(jsoncons::make_unique<filter_selector>(path_expression_type(std::move(toks)))));
                    }
                    break;
                }
                case token_kind::begin_expression:
                    output_stack_.emplace_back(std::move(tok));
                    operator_stack_.emplace_back(token_type(lparen_arg));
                    break;
                case token_kind::end_expression:
                {
                    unwind_rparen(ec);
                    if (ec)
                    {
                        return;
                    }
                    std::vector<token_type> toks;
                    auto it = output_stack_.rbegin();
                    while (it != output_stack_.rend() && it->type() != token_kind::begin_expression)
                    {
                        toks.insert(toks.begin(), std::move(*it));
                        ++it;
                    }
                    if (it == output_stack_.rend())
                    {
                        JSONCONS_THROW(json_runtime_error<std::runtime_error>("Unbalanced braces"));
                    }
                    ++it;
                    output_stack_.erase(it.base(),output_stack_.end());

                    if (!output_stack_.empty() && output_stack_.back().is_path())
                    {
                        output_stack_.back().selector_->append_selector(jsoncons::make_unique<expression_selector>(path_expression_type(std::move(toks))));
                    }
                    else
                    {
                        output_stack_.emplace_back(token_type(jsoncons::make_unique<expression_selector>(path_expression_type(std::move(toks)))));
                    }
                    break;
                }
                case token_kind::selector:
                {
                    if (!output_stack_.empty() && output_stack_.back().is_path())
                    {
                        output_stack_.back().selector_->append_selector(std::move(tok.selector_));
                    }
                    else
                    {
                        output_stack_.emplace_back(std::move(tok));
                    }
                    break;
                }
                case token_kind::separator:
                    output_stack_.emplace_back(std::move(tok));
                    break;
                case token_kind::begin_union:
                    output_stack_.emplace_back(std::move(tok));
                    break;

                case token_kind::end_union:
                {
                    //unwind_rparen(ec);
                    //if (ec)
                    //{
                    //    return;
                    //}
                    std::vector<path_expression_type> expressions;
                    auto it = output_stack_.rbegin();
                    while (it != output_stack_.rend() && it->type() != token_kind::begin_union)
                    {
                        std::vector<token_type> toks;
                        do
                        {
                            toks.insert(toks.begin(), std::move(*it));
                            ++it;
                        } while (it != output_stack_.rend() && it->type() != token_kind::begin_union && it->type() != token_kind::separator);
                        if (it->type() == token_kind::separator)
                        {
                            ++it;
                        }
                        if (!(toks.front().type() == token_kind::literal || toks.front().type() == token_kind::current_node || toks.front().type() == token_kind::root_node))
                        {
                            toks.emplace(toks.begin(), current_node_arg);
                        }
                        expressions.emplace(expressions.begin(), path_expression_type(std::move(toks)));
                    }
                    if (it == output_stack_.rend())
                    {
                        JSONCONS_THROW(json_runtime_error<std::runtime_error>("Unbalanced braces"));
                    }
                    ++it;
                    output_stack_.erase(it.base(),output_stack_.end());

                    if (!output_stack_.empty() && output_stack_.back().is_path())
                    {
                        output_stack_.back().selector_->append_selector(jsoncons::make_unique<union_selector>(std::move(expressions)));
                    }
                    else
                    {
                        output_stack_.emplace_back(token_type(jsoncons::make_unique<union_selector>(std::move(expressions))));
                    }
                    break;
                }
                case token_kind::lparen:
                    operator_stack_.emplace_back(std::move(tok));
                    break;
                case token_kind::rparen:
                {
                    unwind_rparen(ec);
                    break;
                }
                case token_kind::end_function:
                {
                    unwind_rparen(ec);
                    if (ec)
                    {
                        return;
                    }
                    std::vector<token_type> toks;
                    auto it = output_stack_.rbegin();
                    while (it != output_stack_.rend() && it->type() != token_kind::begin_function)
                    {
                        toks.insert(toks.begin(), std::move(*it));
                        ++it;
                    }
                    if (it == output_stack_.rend())
                    {
                        JSONCONS_THROW(json_runtime_error<std::runtime_error>("Unbalanced braces"));
                    }
                    ++it;
                    output_stack_.erase(it.base(),output_stack_.end());

                    if (!output_stack_.empty() && output_stack_.back().is_path())
                    {
                        output_stack_.back().selector_->append_selector(jsoncons::make_unique<function_expression>(path_expression_type(std::move(toks))));
                    }
                    else
                    {
                        output_stack_.emplace_back(token_type(jsoncons::make_unique<function_expression>(std::move(toks))));
                    }
                    break;
                }
                case token_kind::literal:
                    if (!output_stack_.empty() && (output_stack_.back().type() == token_kind::current_node || output_stack_.back().type() == token_kind::root_node))
                    {
                        output_stack_.back() = std::move(tok);
                    }
                    else
                    {
                        output_stack_.emplace_back(std::move(tok));
                    }
                    break;
                case token_kind::begin_function:
                    output_stack_.emplace_back(std::move(tok));
                    operator_stack_.emplace_back(token_type(lparen_arg));
                    break;
                case token_kind::argument:
                    output_stack_.emplace_back(std::move(tok));
                    break;
                case token_kind::function:
                    operator_stack_.emplace_back(std::move(tok));
                    break;
                case token_kind::root_node:
                case token_kind::current_node:
                    output_stack_.emplace_back(std::move(tok));
                    break;
                case token_kind::unary_operator:
                case token_kind::binary_operator:
                {
                    if (operator_stack_.empty() || operator_stack_.back().is_lparen())
                    {
                        operator_stack_.emplace_back(std::move(tok));
                    }
                    else if (tok.precedence_level() < operator_stack_.back().precedence_level()
                             || (tok.precedence_level() == operator_stack_.back().precedence_level() && tok.is_right_associative()))
                    {
                        operator_stack_.emplace_back(std::move(tok));
                    }
                    else
                    {
                        auto it = operator_stack_.rbegin();
                        while (it != operator_stack_.rend() && it->is_operator()
                               && (tok.precedence_level() > it->precedence_level()
                             || (tok.precedence_level() == it->precedence_level() && tok.is_right_associative())))
                        {
                            output_stack_.emplace_back(std::move(*it));
                            ++it;
                        }

                        operator_stack_.erase(it.base(),operator_stack_.end());
                        operator_stack_.emplace_back(std::move(tok));
                    }
                    break;
                }
                default:
                    break;
            }
        }

        uint32_t append_to_codepoint(uint32_t cp, int c, std::error_code& ec)
        {
            cp *= 16;
            if (c >= '0'  &&  c <= '9')
            {
                cp += c - '0';
            }
            else if (c >= 'a'  &&  c <= 'f')
            {
                cp += c - 'a' + 10;
            }
            else if (c >= 'A'  &&  c <= 'F')
            {
                cp += c - 'A' + 10;
            }
            else
            {
                ec = jsonpath_errc::invalid_codepoint;
            }
            return cp;
        }
    };

    } // namespace detail

    template <class Json,class JsonReference = const Json&>
    class jsonpath_expression
    {
    public:
        using evaluator_t = typename jsoncons::jsonpath::detail::jsonpath_evaluator<Json, JsonReference>;
        using char_type = typename evaluator_t::char_type;
        using string_type = typename evaluator_t::string_type;
        using string_view_type = typename evaluator_t::string_view_type;
        using value_type = typename evaluator_t::value_type;
        using reference = typename evaluator_t::reference;
        using expression_t = typename evaluator_t::path_expression_type;
        using path_node_type = typename evaluator_t::path_node_type;
    private:
        jsoncons::jsonpath::detail::static_resources<value_type,reference> static_resources_;
        expression_t expr_;
    public:
        jsonpath_expression() = default;

        jsonpath_expression(jsoncons::jsonpath::detail::static_resources<value_type,reference>&& resources,
                            expression_t&& expr)
            : static_resources_(std::move(resources)), 
              expr_(std::move(expr))
        {
        }

        template <class Callback>
        typename std::enable_if<jsoncons::detail::is_function_object<Callback,path_node_type&>::value,void>::type
        evaluate(reference instance, Callback callback, result_flags flags = result_flags::value)
        {
            string_type path = {'$'};

            jsoncons::jsonpath::detail::dynamic_resources<Json,reference> resources;
            expr_.evaluate(resources, path, instance, instance, callback, flags);
        }

        Json evaluate(reference instance, result_flags flags = result_flags::value)
        {
            string_type path = {'$'};

            if ((flags & result_flags::value) == result_flags::value)
            {
                jsoncons::jsonpath::detail::dynamic_resources<Json,reference> resources;
                return expr_.evaluate(resources, path, instance, instance, flags);
            }
            else if ((flags & result_flags::path) == result_flags::path)
            {
                jsoncons::jsonpath::detail::dynamic_resources<Json,reference> resources;

                Json result(json_array_arg);
                auto callback = [&result](path_node_type& node)
                {
                    result.emplace_back(node.path);
                };
                expr_.evaluate(resources, path, instance, instance, callback, flags);
                return result;
            }
            else
            {
                return Json(json_array_arg);
            }            
        }

        template<class Source>
        static typename std::enable_if<jsoncons::detail::is_sequence_of<Source,char_type>::value,jsonpath_expression>::type
        compile(const Source& path)
        {
            jsoncons::jsonpath::detail::static_resources<value_type,reference> resources;

            evaluator_t e;
            expression_t expr = e.compile(resources, path.data(), path.length());
            return jsonpath_expression(std::move(resources), std::move(expr));
        }

        static jsonpath_expression compile(const char_type* path)
        {
            return compile(basic_string_view<char_type>(path));
        }

        template<class Source>
        static typename std::enable_if<jsoncons::detail::is_sequence_of<Source,char_type>::value,jsonpath_expression>::type
        compile(const Source& path, std::error_code& ec)
        {
            jsoncons::jsonpath::detail::static_resources<value_type,reference> resources;
            evaluator_t e;
            expression_t expr = e.compile(resources, path, ec);
            return jsonpath_expression(std::move(resources), std::move(expr));
        }

        static jsonpath_expression compile(const char_type* path, std::error_code& ec)
        {
            return compile(string_view_type(path), ec);
        }
    };

    template<class Json,class Source>
    typename std::enable_if<jsoncons::detail::is_sequence_of<Source,typename Json::char_type>::value,jsonpath_expression<Json>>::type
    make_expression(const Source& expr)
    {
        return jsonpath_expression<Json>::compile(expr);
    }

    template <class Json>
    jsonpath_expression<Json> make_expression(const typename Json::char_type* expr)
    {
        return jsonpath_expression<Json>::compile(expr);
    }

    template<class Json,class Source>
    typename std::enable_if<jsoncons::detail::is_sequence_of<Source,typename Json::char_type>::value,jsonpath_expression<Json>>::type
    make_expression(const Source& expr, std::error_code& ec)
    {
        return jsonpath_expression<Json>::compile(expr, ec);
    }

    template <class Json>
    jsonpath_expression<Json> make_expression(const typename Json::char_type* expr, std::error_code& ec)
    {
        return make_expression(basic_string_view<typename Json::char_type>(expr), ec);
    }

    template<class Json,class Source>
    typename std::enable_if<jsoncons::detail::is_sequence_of<Source,typename Json::char_type>::value,Json>::type
    json_query(const Json& instance, 
               const Source& path, 
               result_flags flags = result_flags::value)
    {
        auto expression = make_expression<Json>(path);
        return expression.evaluate(instance, flags);
    }

    template<class Json>
    Json json_query(const Json& instance, 
                    const typename Json::char_type* path, 
                    result_flags flags = result_flags::value)
    {
        return json_query(instance, basic_string_view<typename Json::char_type>(path), flags);
    }

    template<class Json, class Source, class T>
    typename std::enable_if<jsoncons::detail::is_sequence_of<Source,typename Json::char_type>::value &&
                            !jsoncons::detail::is_function_object<T,Json>::value,void>::type
    json_replace(Json& instance, const Source& path, const T& new_value)
    {
        using evaluator_t = typename jsoncons::jsonpath::detail::jsonpath_evaluator<Json, Json&>;
        using string_type = typename evaluator_t::string_type;
        using value_type = typename evaluator_t::value_type;
        using reference = typename evaluator_t::reference;
        using expression_t = typename evaluator_t::path_expression_type;
        using path_node_type = typename evaluator_t::path_node_type;

        string_type output_path = { '$' };

        jsoncons::jsonpath::detail::static_resources<value_type,reference> static_resources;
        evaluator_t e;
        expression_t expr = e.compile(static_resources, path);

        jsoncons::jsonpath::detail::dynamic_resources<Json,reference> resources;
        auto callback = [&new_value](path_node_type& node)
        {
            *node.ptr = new_value;
        };
        expr.evaluate(resources, output_path, instance, instance, callback, result_flags::no_dups);
    }

    template<class Json, class Source, class Op>
    typename std::enable_if<jsoncons::detail::is_sequence_of<Source,typename Json::char_type>::value &&
                            jsoncons::detail::is_function_object<Op,Json>::value,void>::type
    json_replace(Json& instance, const Source& path, Op op)
    {
        using evaluator_t = typename jsoncons::jsonpath::detail::jsonpath_evaluator<Json, Json&>;
        using string_type = typename evaluator_t::string_type;
        using value_type = typename evaluator_t::value_type;
        using reference = typename evaluator_t::reference;
        using expression_t = typename evaluator_t::path_expression_type;
        using path_node_type = typename evaluator_t::path_node_type;

        string_type output_path = { '$' };

        jsoncons::jsonpath::detail::static_resources<value_type,reference> static_resources;
        evaluator_t e;
        expression_t expr = e.compile(static_resources, path);

        jsoncons::jsonpath::detail::dynamic_resources<Json,reference> resources;
        auto callback = [op](path_node_type& node)
        {
            *node.ptr = op(*node.ptr);
        };
        expr.evaluate(resources, output_path, instance, instance, callback, result_flags::no_dups);
    }

    template<class Json, class Op>
    void json_replace(Json& instance, const typename Json::char_type* path, Op op)
    {
        json_replace(instance, basic_string_view<typename Json::char_type>(path), op);
    }

} // namespace jsonpath
} // namespace jsoncons

#endif
