// SPDX-License-Identifier: GPL-3.0-or-later
package org.rigctld.android;

import android.content.Context;
import android.widget.ArrayAdapter;
import android.widget.Filter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

final class RigModelAdapter extends ArrayAdapter<String> {
    private final List<String> all;
    private final Filter filter = new Filter() {
        @Override protected FilterResults performFiltering(CharSequence constraint) {
            String query = constraint == null ? "" : constraint.toString().trim().toLowerCase(Locale.ROOT);
            List<String> matches = new ArrayList<>();
            for (String model : all) {
                if (model.toLowerCase(Locale.ROOT).contains(query)) matches.add(model);
            }
            FilterResults result = new FilterResults();
            result.values = matches;
            result.count = matches.size();
            return result;
        }

        @Override protected void publishResults(CharSequence constraint, FilterResults results) {
            clear();
            @SuppressWarnings("unchecked")
            List<String> matches = (List<String>) results.values;
            if (matches != null) addAll(matches);
            notifyDataSetChanged();
        }
    };

    RigModelAdapter(Context context, List<String> models) {
        super(context, android.R.layout.simple_dropdown_item_1line, new ArrayList<>(models));
        all = List.copyOf(models);
    }

    @Override public Filter getFilter() {
        return filter;
    }
}
