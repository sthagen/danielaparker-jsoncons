// Copyright 2013 Daniel Parker
// Distributed under the Boost license, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// See https://github.com/danielaparker/jsoncons for latest version

#ifndef JSONCONS_JSONPATH_JSON_QUERY_HPP
#define JSONCONS_JSONPATH_JSON_QUERY_HPP

#include <array> // std::array
#include <string>
#include <vector>
#include <memory>
#include <type_traits> // std::is_const
#include <limits> // std::numeric_limits
#include <utility> // std::move
#include <regex>
#include <set> // std::set
#include <iterator> // std::make_move_iterator
#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonpath_new/jsonpath_filter.hpp>
#include <jsoncons_ext/jsonpath_new/jsonpath_error.hpp>
#include <jsoncons_ext/jsonpath_new/jsonpath_expression.hpp>
#include <jsoncons_ext/jsonpath_new/jsonpath_function.hpp>

namespace jsoncons { namespace jsonpath_new {

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
        rhs_expression,
        recursive_descent_or_lhs_expression,
        lhs_expression,
        name_or_left_bracket,
        unquoted_name,
        unquoted_name2,
        single_quoted_name,
        double_quoted_name,
        bracketed_unquoted_name_or_union,
        union_expression,
        single_quoted_name_or_union,
        double_quoted_name_or_union,
        identifier_or_union,
        wildcard_or_union,
        bracket_specifier_or_union,
        index_or_slice_expression,
        number,
        digit,
        rhs_slice_expression_start,
        rhs_slice_expression_stop,
        comma_or_right_bracket,
        expect_right_bracket,
        path_or_function_name,
        function,
        arg_or_right_paren,
        path_argument,
        unquoted_arg,
        single_quoted_arg,
        double_quoted_arg,
        more_args_or_right_paren
    };

    JSONCONS_STRING_LITERAL(length_literal, 'l', 'e', 'n', 'g', 't', 'h')

    template<class Json,
             class JsonReference,
             class PathCons>
    class jsonpath_evaluator : public ser_context
    {
    public:
        using char_type = typename Json::char_type;
        using string_type = std::basic_string<char_type,std::char_traits<char_type>>;
        using string_view_type = typename Json::string_view_type;
        using path_node_type = path_node<Json,JsonReference>;
        using reference = JsonReference;
        using pointer = typename path_node_type::pointer;
        using selector_base_type = selector_base<Json,JsonReference>;
        using path_token_type = path_token<Json,JsonReference>;
        using path_expression_type = path_expression<Json,JsonReference>;

    private:
        class identifier_selector final : public selector_base_type
        {
        private:
            string_type identifier_;
        public:
            identifier_selector(const string_view_type& identifier)
                : identifier_(identifier)
            {
            }

            void select(jsonpath_resources<Json>& /*resources*/,
                        const string_type& path, reference val,
                        std::vector<path_node_type>& nodes) const override
            {
                if (val.is_object())
                {
                    auto it = val.find(identifier_);
                    if (it != val.object_range().end())
                    {
                        nodes.emplace_back(PathCons()(path,identifier_),std::addressof(it->value()));
                    }
                }
            }
        };

        class index_selector final : public selector_base_type
        {
            int64_t index_;
        public:
            index_selector(int64_t index)
                : index_(index)
            {
            }

            void select(jsonpath_resources<Json>& /*resources*/,
                        const string_type& path, reference val,
                        std::vector<path_node_type>& nodes) const override
            {
                if (val.is_array())
                {
                    int64_t slen = static_cast<int64_t>(val.size());
                    if (index_ >= 0 && index_ < slen)
                    {
                        std::size_t index = static_cast<std::size_t>(index_);
                        nodes.emplace_back(PathCons()(path,""),std::addressof(val.at(index)));
                    }
                    else if ((slen + index_) >= 0 && (slen+index_) < slen)
                    {
                        std::size_t index = static_cast<std::size_t>(slen + index_);
                        nodes.emplace_back(PathCons()(path,""),std::addressof(val.at(index)));
                    }
                }
            }
        };

        // projection_base
        class projection_base : public selector_base_type
        {
        protected:
            std::vector<std::unique_ptr<selector_base_type>> selectors_;
        public:
            projection_base(std::size_t precedence_level)
                : selector_base_type(true, false, precedence_level)
            {
            }

            void add_selector(std::unique_ptr<selector_base_type>&& expr) override
            {
                if (!selectors_.empty() && selectors_.back()->is_projection() && 
                    (expr->precedence_level() < selectors_.back()->precedence_level() ||
                     (expr->precedence_level() == selectors_.back()->precedence_level() && expr->is_right_associative())))
                {
                    selectors_.back()->add_selector(std::move(expr));
                }
                else
                {
                    selectors_.emplace_back(std::move(expr));
                }
            }

            void apply_expressions(jsonpath_resources<Json>& resources,
                                   const string_type& path, 
                                   reference val,
                                   std::vector<path_node_type>& nodes) const
            {
                if (selectors_.empty())
                {
                    nodes.emplace_back(path, std::addressof(val));
                }
                else
                {
                    std::vector<path_node_type> collect;
                    collect.emplace_back(path,std::addressof(val));
                    for (auto& selector : selectors_)
                    {
                        std::vector<path_node_type> temp;
                        for (auto& item : collect)
                            selector->select(resources, path, *(item.val_ptr), temp);
                        collect = std::move(temp);
                    }
                    for (auto& item : collect)
                    {
                        nodes.emplace_back(std::move(item));
                    }
                }
            }
        };

        class wildcard_selector final : public projection_base
        {
        private:
        public:
            wildcard_selector()
                : projection_base(11)
            {
            }

            void select(jsonpath_resources<Json>& resources,
                        const string_type& path, reference val,
                        std::vector<path_node_type>& nodes) const override
            {
                if (val.is_array())
                {
                    for (auto& item : val.array_range())
                    {
                        this->apply_expressions(resources, path, item, nodes);
                    }
                }
                else if (val.is_object())
                {
                    for (auto& item : val.object_range())
                    {
                        this->apply_expressions(resources, path, item.value(), nodes);
                    }
                }
            }
        };

        class union_selector final : public selector_base_type
        {
            std::vector<path_expression_type> expressions_;
        public:
            union_selector(std::vector<path_expression_type>&& expressions)
                : expressions_(std::move(expressions))
            {
            }

            void select(jsonpath_resources<Json>& resources,
                        const string_type& /*path*/, 
                        reference val, 
                        std::vector<path_node_type>& nodes) const override
            {
                auto callback = [&nodes](path_node_type& node)
                {
                    nodes.push_back(node);
                };
                for (auto& expr : expressions_)
                {
                    expr.evaluate(resources, val, callback);
                }
            }
        };

        class expression_selector final : public selector_base_type
        {
        private:
             jsonpath_filter_expr<Json> result_;
        public:
            expression_selector(jsonpath_filter_expr<Json>&& result)
                : result_(std::move(result))
            {
            }

            void select(jsonpath_resources<Json>& resources,
                        const string_type& path, 
                        reference val, 
                        std::vector<path_node_type>& nodes) const override
            {
                auto index = result_.eval(resources, val);
                if (index.template is<std::size_t>())
                {
                    std::size_t start = index.template as<std::size_t>();
                    if (val.is_array() && start < val.size())
                    {
                        nodes.emplace_back(PathCons()(path,start),std::addressof(val[start]));
                    }
                }
                else if (index.is_string())
                {
                    identifier_selector selector(index.as_string_view());
                    selector.select(resources, path, val, nodes);
                }
            }
        };

        class filter_selector final : public projection_base
        {
        private:
             jsonpath_filter_expr<Json> result_;
        public:
            filter_selector(jsonpath_filter_expr<Json>&& result)
                : projection_base(11), result_(std::move(result))
            {
            }

            void select(jsonpath_resources<Json>& resources,
                        const string_type& path, 
                        reference val, 
                        std::vector<path_node_type>& nodes) const override
            {
                if (val.is_array())
                {
                    for (std::size_t i = 0; i < val.size(); ++i)
                    {
                        if (result_.exists(resources, val[i]))
                        {
                            nodes.emplace_back(PathCons()(path,i),std::addressof(val[i]));
                        }
                    }
                }
                else if (val.is_object())
                {
                    if (result_.exists(resources, val))
                    {
                        nodes.emplace_back(path, std::addressof(val));
                    }
                }
                
            }
        };

        class slice_selector final : public projection_base
        {
        private:
            slice slice_;
        public:
            slice_selector(const slice& slic)
                : projection_base(11), slice_(slic) 
            {
            }

            void select(jsonpath_resources<Json>& resources,
                        const string_type& path, 
                        reference val,
                        std::vector<path_node_type>& nodes) const override
            {
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
                            //nodes.emplace_back(PathCons()(path,j),std::addressof(val[j]));
                            this->apply_expressions(resources, path, val[j], nodes);
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
                                //nodes.emplace_back(PathCons()(path,j),std::addressof(val[j]));
                                this->apply_expressions(resources, path, val[j], nodes);
                            }
                        }
                    }
                }
            }
        };

        function_table<Json,pointer> functions_;

        std::size_t line_;
        std::size_t column_;
        const char_type* begin_input_;
        const char_type* end_input_;
        const char_type* p_;

        using argument_type = std::vector<pointer>;
        std::vector<argument_type> function_stack_;
        std::vector<path_state> state_stack_;
        std::vector<path_token_type> token_stack_;

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
/*
        void call_function(jsonpath_resources<Json>& resources, const string_type& function_name, std::error_code& ec)
        {
            auto f = functions_.get(function_name, ec);
            if (ec)
            {
                return;
            }
            auto result = f(function_stack_, ec);
            if (ec)
            {
                return;
            }

            string_type s = {'$'};
            std::vector<path_node_type> v;
            pointer ptr = resources.create_temp(std::move(result));
            v.emplace_back(s,ptr);
            stack_.push_back(v);
        }
*/
/*
        template<class Op>
        void replace_fn(Op op)
        {
            if (!stack_.empty())
            {
                for (std::size_t i = 0; i < stack_.back().size(); ++i)
                {
                    *(stack_.back()[i].val_ptr) = op(*(stack_.back()[i].val_ptr));
                }
            }
        }

        template <class T>
        void replace(T&& new_value)
        {
            if (!stack_.empty())
            {
                for (std::size_t i = 0; i < stack_.back().size(); ++i)
                {
                    *(stack_.back()[i].val_ptr) = new_value;
                }
            }
        }
*/
        path_expression_type compile(jsonpath_resources<Json>& resources, const string_view_type& path)
        {
            std::error_code ec;
            auto result = compile(resources, path.data(), path.length(), ec);
            if (ec)
            {
                JSONCONS_THROW(jsonpath_error(ec, line_, column_));
            }
            return std::move(result);
        }

        path_expression_type compile(jsonpath_resources<Json>& resources, const string_view_type& path, std::error_code& ec)
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

        path_expression_type compile(jsonpath_resources<Json>& resources,
                                     const char_type* path, 
                                     std::size_t length)
        {
            std::error_code ec;
            auto result = compile(resources, path, length, ec);
            if (ec)
            {
                JSONCONS_THROW(jsonpath_error(ec, line_, column_));
            }
            return std::move(result);
        }

        path_expression_type compile(jsonpath_resources<Json>& resources,
                                     const char_type* path, 
                                     std::size_t length,
                                     std::error_code& ec)
        {

            state_stack_.emplace_back(path_state::start);

            string_type function_name;
            string_type buffer;

            begin_input_ = path;
            end_input_ = path + length;
            p_ = begin_input_;

            string_type s = {'$'};
            //std::vector<path_node_type> v;
            //v.emplace_back(std::move(s),std::addressof(root));
            //stack_.push_back(v);

            slice slic;
            std::size_t save_line = 1;
            std::size_t save_column = 1;

            while (p_ < end_input_)
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
                                state_stack_.emplace_back(path_state::rhs_expression);
                                ++p_;
                                ++column_;
                                break;
                            }
                            default:
                            {
                                switch (*p_)
                                {
                                    case '.':
                                    case '[':
                                        ec = jsonpath_errc::expected_root;
                                        return path_expression_type();
                                    default: // might be function, validate name later
                                        state_stack_.emplace_back(path_state::rhs_expression);
                                        state_stack_.emplace_back(path_state::path_or_function_name);
                                        break;
                                }
                                break;
                            }
                        };
                        break;
                    }
                    case path_state::path_or_function_name:
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                            {
                                push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));

                                //apply_selectors(resources);
                                buffer.clear();
                                state_stack_.pop_back();
                                advance_past_space_character();
                                break;
                            }
                            case '(':
                                state_stack_.back() = path_state::arg_or_right_paren;
                                function_name = buffer;
                                buffer.clear();
                                ++p_;
                                ++column_;
                                save_line = line_;
                                save_column = column_;
                                break;
                            case '[':
                            {
                                if (buffer.size() > 0)
                                {
                                    push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));
                                    //apply_selectors(resources);
                                    buffer.clear();
                                }
                                buffer.clear();

                                state_stack_.back() = path_state::bracket_specifier_or_union;
                                ++p_;
                                ++column_;
                                break;
                            }
                            case '.':
                            {
                                if (buffer.size() > 0)
                                {
                                    push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));
                                    //apply_selectors(resources);
                                    buffer.clear();
                                }
                                state_stack_.back() = path_state::recursive_descent_or_lhs_expression;
                                ++p_;
                                ++column_;
                                break;
                            }
                            case '*':
                            {
                                push_token(path_token_type(jsoncons::make_unique<wildcard_selector>()));
                                state_stack_.back() = path_state::recursive_descent_or_lhs_expression;
                                ++p_;
                                ++column_;
                                break;
                            }
                            case '\'':
                            {
                                buffer.clear();
                                state_stack_.back() = path_state::single_quoted_name;
                                ++p_;
                                ++column_;
                                break;
                            }
                            case '\"':
                            {
                                buffer.clear();
                                state_stack_.back() = path_state::double_quoted_name;
                                ++p_;
                                ++column_;
                                break;
                            }
                            default:
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                        }
                        break;
                    case path_state::arg_or_right_paren:
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '$':
                                buffer.clear();
                                buffer.push_back(*p_);
                                state_stack_.emplace_back(path_state::path_argument);
                                ++p_;
                                ++column_;
                                break;
                            case '\'':
                                buffer.clear();
                                buffer.push_back('\"');
                                state_stack_.back() = path_state::more_args_or_right_paren;
                                state_stack_.emplace_back(path_state::single_quoted_arg);
                                ++p_;
                                ++column_;
                                break;
                            case '\"':
                                buffer.clear();
                                buffer.push_back('\"');
                                state_stack_.back() = path_state::more_args_or_right_paren;
                                state_stack_.emplace_back(path_state::double_quoted_arg);
                                ++p_;
                                ++column_;
                                break;
                            case ')': // revisit
                            {
                                /*jsonpath_evaluator<Json,JsonReference,PathCons> evaluator(save_line, save_column);
                                evaluator.evaluate(resources, root, buffer, ec);
                                if (ec)
                                {
                                    line_ = evaluator.line();
                                    column_ = evaluator.column();
                                    return path_expression_type();
                                }
                                function_stack_.push_back(evaluator.get_pointers());

                                call_function(resources, function_name, ec);
                                if (ec)
                                {
                                    return path_expression_type();
                                }
                                state_stack_.pop_back(); */
                                ++p_;
                                ++column_;
                                break;
                            }
                            default:
                                buffer.clear();
                                state_stack_.back() = path_state::more_args_or_right_paren;
                                state_stack_.emplace_back(path_state::unquoted_arg);
                                ++p_;
                                ++column_;
                                break;
                        }
                        break;
                    case path_state::path_argument: // revisit
                        switch (*p_)
                        {
                            case ',':
                            {
                                /*jsonpath_evaluator<Json, JsonReference, PathCons> evaluator;
                                evaluator.evaluate(resources, root, buffer, ec);
                                if (ec)
                                {
                                    return path_expression_type();
                                }
                                function_stack_.push_back(evaluator.get_pointers());*/
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            }
                            case ')':
                            {
                                state_stack_.pop_back();
                                break;
                            }
                            default:
                                buffer.push_back(*p_); // path_argument
                                ++p_;
                                ++column_;
                                break;
                        }
                        break;
                    case path_state::unquoted_arg:
                        switch (*p_)
                        {
                            case ',':
                                JSONCONS_TRY
                                {
                                    auto val = Json::parse(buffer);
                                    auto temp = resources.create_temp(val);
                                    function_stack_.push_back(std::vector<pointer>{temp});
                                }
                                JSONCONS_CATCH(const ser_error&)     
                                {
                                    ec = jsonpath_errc::argument_parse_error;
                                    return path_expression_type();
                                }
                                buffer.clear();
                                //state_ = path_state::arg_or_right_paren;
                                state_stack_.pop_back();
                                break;
                            case ')':
                            {
                                JSONCONS_TRY
                                {
                                    auto val = Json::parse(buffer);
                                    auto temp = resources.create_temp(val);
                                    function_stack_.push_back(std::vector<pointer>{temp});
                                }
                                JSONCONS_CATCH(const ser_error&)     
                                {
                                    ec = jsonpath_errc::argument_parse_error;
                                    return path_expression_type();
                                }
                                //call_function(resources, function_name, ec);
                                if (ec)
                                {
                                    return path_expression_type();
                                }
                                state_stack_.pop_back();
                                break;
                            }
                            default:
                                buffer.push_back(*p_);
                                break;
                        }
                        ++p_;
                        ++column_;
                        break;
                    case path_state::single_quoted_arg:
                        switch (*p_)
                        {
                            case '\'':
                                buffer.push_back('\"');
                                state_stack_.pop_back();
                                break;
                            case '\"':
                                buffer.push_back('\\');
                                buffer.push_back('\"');
                                state_stack_.pop_back();
                                break;
                            default:
                                buffer.push_back(*p_);
                                break;
                        }
                        ++p_;
                        ++column_;
                        break;
                    case path_state::double_quoted_arg:
                        switch (*p_)
                        {
                            case '\"':
                                buffer.push_back('\"');
                                state_stack_.pop_back();
                                break;
                            default:
                                buffer.push_back(*p_);
                                break;
                        }
                        ++p_;
                        ++column_;
                        break;
                    case path_state::more_args_or_right_paren:
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case ',':
                                JSONCONS_TRY
                                {
                                    auto val = Json::parse(buffer);
                                    auto temp = resources.create_temp(val);
                                    function_stack_.push_back(std::vector<pointer>{temp});
                                }
                                JSONCONS_CATCH(const ser_error&)     
                                {
                                    ec = jsonpath_errc::argument_parse_error;
                                    return path_expression_type();
                                }
                                buffer.clear();
                                //state_ = path_state::arg_or_right_paren;
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            case ')':
                            {
                                JSONCONS_TRY
                                {
                                    auto val = Json::parse(buffer);
                                    auto temp = resources.create_temp(val);
                                    function_stack_.push_back(std::vector<pointer>{temp});
                                }
                                JSONCONS_CATCH(const ser_error&)     
                                {
                                    ec = jsonpath_errc::argument_parse_error;
                                    return path_expression_type();
                                }
                                //call_function(resources, function_name, ec);
                                if (ec)
                                {
                                    return path_expression_type();
                                }
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            }
                            default:
                                ec = jsonpath_errc::invalid_filter_unsupported_operator;
                                return path_expression_type();
                        }
                        break;
                    case path_state::recursive_descent_or_lhs_expression:
                        switch (*p_)
                        {
                            case '.':
                                push_token(path_token_type(recursive_descent_arg));
                                ++p_;
                                ++column_;
                                state_stack_.back() = path_state::name_or_left_bracket;
                                break;
                            default:
                                state_stack_.back() = path_state::lhs_expression;
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
                                state_stack_.back() = path_state::lhs_expression;
                                break;
                        }
                        break;
                    case path_state::lhs_expression: 
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '*':
                                push_token(path_token_type(jsoncons::make_unique<wildcard_selector>()));
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            case '\'':
                                state_stack_.back() = path_state::single_quoted_name;
                                ++p_;
                                ++column_;
                                break;
                            case '\"':
                                state_stack_.back() = path_state::double_quoted_name;
                                ++p_;
                                ++column_;
                                break;
                            case '[':
                                state_stack_.emplace_back(path_state::bracket_specifier_or_union);
                                ++p_;
                                ++column_;
                                break;
                            case '.':
                                ec = jsonpath_errc::expected_key;
                                return path_expression_type();
                            default:
                                buffer.clear();
                                state_stack_.back() = path_state::unquoted_name;
                                break;
                        }
                        break;
                    case path_state::rhs_expression: 
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '.':
                                state_stack_.emplace_back(path_state::recursive_descent_or_lhs_expression);
                                ++p_;
                                ++column_;
                                break;
                            case '[':
                                state_stack_.emplace_back(path_state::bracket_specifier_or_union);
                                ++p_;
                                ++column_;
                                break;
                            default:
                                ec = jsonpath_errc::expected_separator;
                                return path_expression_type();
                        };
                        break;
                    case path_state::unquoted_name: 
                        switch (*p_)
                        {
                            case ']':
                            case '[':
                            case '.':
                            case ',':
                            case ' ':case '\t':
                            case '\r':
                            case '\n':
                                state_stack_.back() = path_state::unquoted_name2;
                                break;
                            default:
                                buffer.push_back(*p_);
                                ++p_;
                                ++column_;
                                break;
                        };
                        break;
                    case path_state::unquoted_name2: 
                        switch (*p_)
                        {
                            case ' ':case '\t':case '\r':case '\n':
                                advance_past_space_character();
                                break;
                            case '[':
                                push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));
                                //apply_selectors(resources);
                                buffer.clear();
                                state_stack_.pop_back();
                                break;
                            case ']':
                            case '.':
                            case ',':
                                push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));
                                //apply_selectors(resources);
                                buffer.clear();
                                state_stack_.pop_back();
                                break;
                            default:
                                ec = jsonpath_errc::expected_key;
                                return path_expression_type();
                        };
                        break;
                    case path_state::single_quoted_name:
                        switch (*p_)
                        {
                            case '\'':
                                push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));
                                //apply_selectors(resources);
                                buffer.clear();
                                state_stack_.pop_back();
                                break;
                            case '\\':
                                if (p_+1 < end_input_)
                                {
                                    ++p_;
                                    ++column_;
                                    buffer.push_back(*p_);
                                }
                                else
                                {
                                    ec = jsonpath_errc::unexpected_end_of_input;
                                    return path_expression_type();
                                }
                                break;
                            default:
                                buffer.push_back(*p_);
                                break;
                        };
                        ++p_;
                        ++column_;
                        break;
                    case path_state::double_quoted_name: 
                        switch (*p_)
                        {
                            case '\"':
                                push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));
                                //apply_selectors(resources);
                                buffer.clear();
                                state_stack_.pop_back();
                                break;
                            case '\\':
                                if (p_+1 < end_input_)
                                {
                                    ++p_;
                                    ++column_;
                                    buffer.push_back(*p_);
                                }
                                else
                                {
                                    ec = jsonpath_errc::unexpected_end_of_input;
                                    return path_expression_type();
                                }
                                break;
                            default:
                                buffer.push_back(*p_);
                                break;
                        };
                        ++p_;
                        ++column_;
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
                                //apply_selectors(resources);
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
                                jsonpath_filter_parser<Json> parser(line_,column_);
                                auto result = parser.parse(resources, p_,end_input_,&p_);
                                line_ = parser.line();
                                column_ = parser.column();
                                push_token(path_token_type(jsoncons::make_unique<expression_selector>(std::move(result))));
                                state_stack_.back() = path_state::expect_right_bracket;
                                break;
                            }
                            case '?':
                            {
                                jsonpath_filter_parser<Json> parser(line_,column_);
                                auto result = parser.parse(resources,p_,end_input_,&p_);
                                line_ = parser.line();
                                column_ = parser.column();
                                push_token(path_token_type(jsoncons::make_unique<filter_selector>(std::move(result))));
                                state_stack_.back() = path_state::expect_right_bracket;
                                break;                   
                            }
                            case ':': // slice_expression
                                state_stack_.back() = path_state::rhs_slice_expression_start ;
                                state_stack_.emplace_back(path_state::number);
                                ++p_;
                                ++column_;
                                break;
                            case '*':
                                state_stack_.back() = path_state::wildcard_or_union;
                                //push_token(path_token_type(jsoncons::make_unique<wildcard_selector>()));
                                //state_stack_.back() = path_state::identifier_or_union;
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
                            // number
                            case '-':case '0':case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':
                                state_stack_.back() = path_state::index_or_slice_expression;
                                state_stack_.emplace_back(path_state::number);
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

                    case path_state::number:
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
                    case path_state::index_or_slice_expression:
                        switch(*p_)
                        {
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
                                    push_token(path_token_type(jsoncons::make_unique<index_selector>(r.value())));

                                    buffer.clear();
                                }
                                state_stack_.pop_back(); // bracket_specifier_or_union
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
                                state_stack_.back() = path_state::rhs_slice_expression_start;
                                state_stack_.emplace_back(path_state::number);
                                ++p_;
                                ++column_;
                                break;
                            }
                            default:
                                ec = jsonpath_errc::expected_right_bracket;
                                return path_expression_type();
                        }
                        break;
                    case path_state::rhs_slice_expression_start:
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
                            case ']':
                                push_token(path_token_type(jsoncons::make_unique<slice_selector>(slic)));
                                slic = slice{};
                                state_stack_.pop_back(); // bracket_specifier2
                                ++p_;
                                ++column_;
                                break;
                            case ':':
                                state_stack_.back() = path_state::rhs_slice_expression_stop;
                                state_stack_.emplace_back(path_state::number);
                                ++p_;
                                ++column_;
                                break;
                            default:
                                ec = jsonpath_errc::expected_right_bracket;
                                return path_expression_type();
                        }
                        break;
                    }
                    case path_state::rhs_slice_expression_stop:
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
                            case ']':
                                push_token(path_token_type(jsoncons::make_unique<slice_selector>(slic)));
                                buffer.clear();
                                slic = slice{};
                                state_stack_.pop_back(); // rhs_slice_expression_stop
                                ++p_;
                                ++column_;
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
                            case ']': 
                                push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));
                                buffer.clear();
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            case '.':
                                push_token(path_token_type(begin_union_arg));
                                push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));
                                buffer.clear();
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::lhs_expression);                                
                                ++p_;
                                ++column_;
                                break;
                            case '[':
                                push_token(path_token_type(begin_union_arg));
                                push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::lhs_expression);                                
                                ++p_;
                                ++column_;
                                break;
                            case ',': 
                                push_token(path_token_type(begin_union_arg));
                                push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));
                                push_token(path_token_type(separator_arg));
                                buffer.clear();
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::lhs_expression);                                
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
                                state_stack_.emplace_back(path_state::lhs_expression);
                                ++p_;
                                ++column_;
                                break;
                            case '[':
                                state_stack_.emplace_back(path_state::bracket_specifier_or_union);
                                ++p_;
                                ++column_;
                                break;
                            case ',': 
                                push_token(path_token_type(separator_arg));
                                state_stack_.emplace_back(path_state::lhs_expression);
                                ++p_;
                                ++column_;
                                break;
                            case ']': 
                                push_token(path_token_type(end_union_arg));
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
                                push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));
                                buffer.clear();
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            case '.':
                                push_token(path_token_type(begin_union_arg));
                                push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));
                                buffer.clear();
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::lhs_expression);                                
                                ++p_;
                                ++column_;
                                break;
                            case '[':
                                push_token(path_token_type(begin_union_arg));
                                push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::lhs_expression);                                
                                ++p_;
                                ++column_;
                                break;
                            case ',': 
                                push_token(path_token_type(begin_union_arg));
                                push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));
                                push_token(path_token_type(separator_arg));
                                buffer.clear();
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::lhs_expression);                                
                                ++p_;
                                ++column_;
                                break;
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
                                push_token(path_token_type(jsoncons::make_unique<wildcard_selector>()));
                                buffer.clear();
                                state_stack_.pop_back();
                                ++p_;
                                ++column_;
                                break;
                            case '.':
                                push_token(path_token_type(begin_union_arg));
                                push_token(path_token_type(jsoncons::make_unique<wildcard_selector>()));
                                buffer.clear();
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::lhs_expression);                                
                                ++p_;
                                ++column_;
                                break;
                            case '[':
                                push_token(path_token_type(begin_union_arg));
                                push_token(path_token_type(jsoncons::make_unique<wildcard_selector>()));
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::lhs_expression);                                
                                ++p_;
                                ++column_;
                                break;
                            case ',': 
                                push_token(path_token_type(begin_union_arg));
                                push_token(path_token_type(jsoncons::make_unique<wildcard_selector>()));
                                push_token(path_token_type(separator_arg));
                                buffer.clear();
                                state_stack_.back() = path_state::union_expression; // union
                                state_stack_.emplace_back(path_state::lhs_expression);                                
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
                                if (p_+1 < end_input_)
                                {
                                    ++p_;
                                    ++column_;
                                    buffer.push_back(*p_);
                                }
                                else
                                {
                                    ec = jsonpath_errc::unexpected_end_of_input;
                                    return path_expression_type();
                                }
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
                                if (p_+1 < end_input_)
                                {
                                    ++p_;
                                    ++column_;
                                    buffer.push_back(*p_);
                                }
                                else
                                {
                                    ec = jsonpath_errc::unexpected_end_of_input;
                                    return path_expression_type();
                                }
                                break;
                            default:
                                buffer.push_back(*p_);
                                break;
                        };
                        ++p_;
                        ++column_;
                        break;
                    default:
                        ++p_;
                        ++column_;
                        break;
                }
            }

            switch (state_stack_.back())
            {
                case path_state::unquoted_name: 
                case path_state::unquoted_name2: 
                {
                    push_token(path_token_type(jsoncons::make_unique<identifier_selector>(buffer)));
                    //apply_selectors(resources);
                    buffer.clear();
                    state_stack_.pop_back(); // unquoted_name
                    break;
                }
                default:
                    break;
            }

            if (state_stack_.size() > 2)
            {
                ec = jsonpath_errc::unexpected_end_of_input;
                return path_expression_type();
            }

            /*switch (state_stack_.back())
            {
                case path_state::start:
                {
                    JSONCONS_ASSERT(!stack_.empty());
                    stack_.clear();
                    JSONCONS_ASSERT(state_stack_.size() == 1);
                    state_stack_.pop_back();
                    break;
                }
                default:
                {
                    JSONCONS_ASSERT(state_stack_.size() == 2);
                    state_stack_.pop_back(); 
                    JSONCONS_ASSERT(state_stack_.back() == path_state::start);
                    state_stack_.pop_back();
                    break;
                }
            }*/
            return path_expression_type(std::move(token_stack_));
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

        void push_token(path_token_type&& tok)
        {
            switch (tok.type())
            {
                case path_token_kind::selector:
                {
                    if (!token_stack_.empty() && token_stack_.back().is_projection() && 
                        (tok.precedence_level() < token_stack_.back().precedence_level() ||
                        (tok.precedence_level() == token_stack_.back().precedence_level() && tok.is_right_associative())))
                    {
                        token_stack_.back().selector_->add_selector(std::move(tok.selector_));
                    }
                    else
                    {
                        token_stack_.emplace_back(std::move(tok));
                    }
                    break;
                }
                case path_token_kind::recursive_descent:
                {
                    token_stack_.emplace_back(std::move(tok));
                    break;
                }
                case path_token_kind::separator:
                    token_stack_.emplace_back(std::move(tok));
                    break;
                case path_token_kind::begin_union:
                    token_stack_.emplace_back(std::move(tok));
                    break;

                case path_token_kind::end_union:
                {
                    std::vector<path_expression_type> expressions;
                    auto it = token_stack_.rbegin();
                    while (it != token_stack_.rend() && it->type() != path_token_kind::begin_union)
                    {
                        std::vector<path_token_type> toks;
                        do
                        {
                            toks.insert(toks.begin(), std::move(*it));
                            ++it;
                        } while (it != token_stack_.rend() && it->type() != path_token_kind::begin_union && it->type() != path_token_kind::separator);
                        if (it->type() == path_token_kind::separator)
                        {
                            ++it;
                        }
                        expressions.emplace(expressions.begin(), path_expression_type(std::move(toks)));
                    }
                    if (it == token_stack_.rend())
                    {
                        JSONCONS_THROW(json_runtime_error<std::runtime_error>("Unbalanced braces"));
                    }
                    ++it;
                    token_stack_.erase(it.base(),token_stack_.end());

                    if (!token_stack_.empty() && token_stack_.back().is_projection() && 
                        (tok.precedence_level() < token_stack_.back().precedence_level() ||
                        (tok.precedence_level() == token_stack_.back().precedence_level() && tok.is_right_associative())))
                    {
                        token_stack_.back().selector_->add_selector(jsoncons::make_unique<union_selector>(std::move(expressions)));
                    }
                    else
                    {
                        token_stack_.emplace_back(path_token_type(jsoncons::make_unique<union_selector>(std::move(expressions))));
                    }
                    break;
                }
                default:
                    break;
            }
        }
    };

    } // namespace detail

    template <class Json>
    class jsonpath_expression
    {
        using evaluator_t = typename jsoncons::jsonpath_new::detail::jsonpath_evaluator<Json, const Json&, detail::VoidPathConstructor<Json>>;
        using expression_t = typename evaluator_t::path_expression_type;
        jsoncons::jsonpath_new::detail::jsonpath_resources<Json> static_resources_;
        expression_t expr_;
    public:
        using string_view_type = typename evaluator_t::string_view_type;
        using reference = typename evaluator_t::reference;

        jsonpath_expression() = default;

        jsonpath_expression(jsoncons::jsonpath_new::detail::jsonpath_resources<Json>&& resources,
                            expression_t&& expr)
            : static_resources_(std::move(resources)), 
              expr_(std::move(expr))
        {
        }

        Json evaluate(reference instance)
        {
            jsoncons::jsonpath_new::detail::jsonpath_resources<Json> dynamic_resources;
            return expr_.evaluate(dynamic_resources, instance);
        }

        static jsonpath_expression compile(const string_view_type& path)
        {
            jsoncons::jsonpath_new::detail::jsonpath_resources<Json> resources;

            evaluator_t e;
            expression_t expr = e.compile(resources, path.data(), path.length());
            return jsonpath_expression(std::move(resources), std::move(expr));
        }

        static jsonpath_expression compile(const string_view_type& path,
                                           std::error_code& ec)
        {
            jsoncons::jsonpath_new::detail::jsonpath_resources<Json> resources;
            evaluator_t e;
            expression_t expr = e.compile(resources, path, ec);
            return jsonpath_expression(std::move(resources), std::move(expr));
        }
    };

    template <class Json>
    jsonpath_expression<Json> make_expression(const typename json::string_view_type& expr)
    {
        return jsonpath_expression<Json>::compile(expr);
    }

    template <class Json>
    jsonpath_expression<Json> make_expression(const typename json::string_view_type& expr,
                                              std::error_code& ec)
    {
        return jsonpath_expression<Json>::compile(expr, ec);
    }

    enum class result_type {value,path};

/*
    template<class Json>
    Json json_query(const Json& root, const typename Json::string_view_type& path, result_type result_t = result_type::value)
    {
        if (result_t == result_type::value)
        {
            jsoncons::jsonpath_new::detail::jsonpath_evaluator<Json,const Json&,detail::VoidPathConstructor<Json>> evaluator;
            jsoncons::jsonpath_new::detail::jsonpath_resources<Json> resources;
            evaluator.evaluate(resources, root, path);
            return evaluator.get_values();
        }
        else
        {
            jsoncons::jsonpath_new::detail::jsonpath_evaluator<Json,const Json&,detail::PathConstructor<Json>> evaluator;
            jsoncons::jsonpath_new::detail::jsonpath_resources<Json> resources;
            evaluator.evaluate(resources, root, path);
            return evaluator.get_normalized_paths();
        }
    }

    template<class Json, class T>
    typename std::enable_if<!jsoncons::detail::is_function_object<T,Json>::value,void>::type
    json_replace(Json& root, const typename Json::string_view_type& path, T&& new_value)
    {
        jsoncons::jsonpath_new::detail::jsonpath_evaluator<Json,Json&,detail::VoidPathConstructor<Json>> evaluator;
        jsoncons::jsonpath_new::detail::jsonpath_resources<Json> resources;
        evaluator.evaluate(resources, root, path);
        evaluator.replace(std::forward<T>(new_value));
    }

    template<class Json, class Op>
    typename std::enable_if<jsoncons::detail::is_function_object<Op,Json>::value,void>::type
    json_replace(Json& root, const typename Json::string_view_type& path, Op op)
    {
        jsoncons::jsonpath_new::detail::jsonpath_evaluator<Json,Json&,detail::VoidPathConstructor<Json>> evaluator;
        jsoncons::jsonpath_new::detail::jsonpath_resources<Json> resources;
        evaluator.evaluate(resources, root, path);
        evaluator.replace_fn(op);
    }

*/

} // namespace jsonpath_new
} // namespace jsoncons

#endif