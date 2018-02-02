#include <Columns/ColumnString.h>
#include <Columns/ColumnsNumber.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypeDateTime.h>
#include <DataStreams/OneBlockInputStream.h>
#include <Storages/System/StorageSystemTables.h>
#include <Storages/VirtualColumnUtils.h>
#include <Databases/IDatabase.h>
#include <Interpreters/Context.h>
#include <DataTypes/DataTypesNumber.h>

namespace DB
{

StorageSystemTables::StorageSystemTables(const std::string & name_)
    : name(name_)
{
    columns = NamesAndTypesList{
        {"database", std::make_shared<DataTypeString>()},
        {"name", std::make_shared<DataTypeString>()},
        {"engine", std::make_shared<DataTypeString>()},
        {"metadata_modification_time", std::make_shared<DataTypeDateTime>()},
        {"temporary", std::make_shared<DataTypeUInt8>()}
    };
}


static ColumnPtr getFilteredDatabases(const ASTPtr & query, const Context & context)
{
    MutableColumnPtr column = ColumnString::create();
    for (const auto & db : context.getDatabases())
        column->insert(db.first);

    Block block { ColumnWithTypeAndName( std::move(column), std::make_shared<DataTypeString>(), "database" ) };
    VirtualColumnUtils::filterBlockWithQuery(query, block, context);
    return block.getByPosition(0).column;
}


BlockInputStreams StorageSystemTables::read(
    const Names & column_names,
    const SelectQueryInfo & query_info,
    const Context & context,
    QueryProcessingStage::Enum & processed_stage,
    const size_t /*max_block_size*/,
    const unsigned /*num_streams*/)
{
    check(column_names);
    processed_stage = QueryProcessingStage::FetchColumns;

    MutableColumns res_columns = getSampleBlock().cloneEmptyColumns();

    ColumnPtr filtered_databases_column = getFilteredDatabases(query_info.query, context);

    for (size_t row_number = 0; row_number < filtered_databases_column->size(); ++row_number)
    {
        std::string database_name = filtered_databases_column->getDataAt(row_number).toString();

        auto database = context.tryGetDatabase(database_name);

        if (!database)
        {
            /// Database was deleted just now.
            continue;
        }

        for (auto iterator = database->getIterator(context); iterator->isValid(); iterator->next())
        {
            auto table_name = iterator->name();
            res_columns[0]->insert(database_name);
            res_columns[1]->insert(table_name);
            res_columns[2]->insert(iterator->table()->getName());
            res_columns[3]->insert(
                static_cast<UInt64>(database->getTableMetadataModificationTime(context, table_name)));
            res_columns[4]->insert(UInt64(0));
        }
    }

    if (context.hasSessionContext())
    {
        Tables external_tables = context.getSessionContext().getExternalTables();

        for (auto table = external_tables.begin(); table != external_tables.end(); table++)
        {
            res_columns[0]->insert(String{});
            res_columns[1]->insert(table->first);
            res_columns[2]->insert(table->second->getName());
            res_columns[3]->insert(UInt64(0));
            res_columns[4]->insert(UInt64(1));
        }
    }



    return BlockInputStreams(1, std::make_shared<OneBlockInputStream>(getSampleBlock().cloneWithColumns(std::move(res_columns))));
}

}
