/********************************************************************\
 * gnc-split-reg2.c -- A widget for the common register look-n-feel *
 * Copyright (C) 1997 Robin D. Clark                                *
 * Copyright (C) 1997-1998 Linas Vepstas <linas@linas.org>          *
 * Copyright (C) 1998 Rob Browning <rlb@cs.utexas.edu>              *
 * Copyright (C) 1999-2000 Dave Peticolas <dave@krondo.com>         *
 * Copyright (C) 2001 Gnumatic, Inc.                                *
 * Copyright (C) 2002,2006 Joshua Sled <jsled@asynchronous.org>     *
 * Copyright (C) 2012 Robert Fewell                                 *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
\********************************************************************/

#include "config.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "gnc-split-reg2.h"
#include "gnc-tree-view-split-reg.h"
#include "gnc-tree-control-split-reg.h"
#include "gnc-ledger-display2.h"

#include "gnc-euro.h"
#include "gnc-gconf-utils.h"
#include "dialog-utils.h"

#define GCONF_SECTION "window/pages/register2"

static QofLogModule log_module = GNC_MOD_GUI;

/***** PROTOTYPES ***************************************************/
void gnc_split_reg2_raise (GNCSplitReg2 *gsr);

static GtkWidget* add_summary_label (GtkWidget *summarybar,
                                     const char *label_str);

static void gnc_split_reg2_determine_read_only (GNCSplitReg2 *gsr);

static void gnc_split_reg2_determine_account_pr (GNCSplitReg2 *gsr);

static GNCPlaceholderType gnc_split_reg2_get_placeholder (GNCSplitReg2 *gsr);
static GtkWidget *gnc_split_reg2_get_parent (GNCLedgerDisplay2 *ledger);

static void gsr2_create_table (GNCSplitReg2 *gsr);
static void gsr2_setup_table (GNCSplitReg2 *gsr);

static void gsr2_setup_status_widgets (GNCSplitReg2 *gsr);

static void gsr2_update_summary_label (GtkWidget *label,
                                      xaccGetBalanceFn getter,
                                      Account *leader,
                                      GNCPrintAmountInfo print_info,
                                      gnc_commodity *cmdty,
                                      gboolean reverse,
                                      gboolean euroFlag );

static void gsr2_redraw_all_cb (GncTreeViewSplitReg *view, gpointer data);

static void gnc_split_reg2_refresh_toolbar (GNCSplitReg2 *gsr);

static void gnc_split_reg2_ld_destroy (GNCLedgerDisplay2 *ledger);

static Transaction* gsr2_create_balancing_transaction (QofBook *book, Account *account,
        time64 statement_date, gnc_numeric balancing_amount);

static void gsr2_emit_simple_signal (GNCSplitReg2 *gsr, const char *sigName);
static void gsr2_emit_help_changed (GncTreeViewSplitReg *view, gpointer user_data);

void gnc_split_reg2_style_ledger_cb (GtkWidget *w, gpointer data);
void gnc_split_reg2_style_auto_ledger_cb (GtkWidget *w, gpointer data);
void gnc_split_reg2_style_journal_cb (GtkWidget *w, gpointer data);
void gnc_split_reg2_double_line_cb (GtkWidget *w, gpointer data);

void gnc_split_reg2_destroy_cb (GtkWidget *widget, gpointer data);

static void gnc_split_reg2_class_init (GNCSplitReg2Class *class);
static void gnc_split_reg2_init (GNCSplitReg2 *gsr);
static void gnc_split_reg2_init2 (GNCSplitReg2 *gsr);


GType
gnc_split_reg2_get_type (void)
{
    static GType gnc_split_reg2_type = 0;

    if (!gnc_split_reg2_type)
    {
        GTypeInfo type_info =
        {
            sizeof(GNCSplitReg2Class),          /* class_size */
            NULL,   			        /* base_init */
            NULL,				/* base_finalize */
            (GClassInitFunc)gnc_split_reg2_class_init,
            NULL,				/* class_finalize */
            NULL,				/* class_data */
            sizeof(GNCSplitReg2),		/* */
            0,				        /* n_preallocs */
            (GInstanceInitFunc)gnc_split_reg2_init,
        };

        gnc_split_reg2_type = g_type_register_static (GTK_TYPE_VBOX,
                             "GNCSplitReg2",
                             &type_info, 0 );
    }

    return gnc_split_reg2_type;
}

/* SIGNALS */
enum
{
    HELP_CHANGED,
    LAST_SIGNAL
};

static guint gnc_split_reg2_signals[LAST_SIGNAL] = { 0 };

static void
gnc_split_reg2_class_init (GNCSplitReg2Class *class)
{
    GtkObjectClass *object_class;

    object_class = (GtkObjectClass*) class;

    gnc_split_reg2_signals[HELP_CHANGED] =
        g_signal_new("help-changed",
                     G_TYPE_FROM_CLASS (object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET (GNCSplitReg2Class, help_changed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE, 0);

    /* Setup the default handlers. */
    class->help_changed = NULL;

}

GtkWidget*
gnc_split_reg2_new (GNCLedgerDisplay2 *ld,
                   GtkWindow *parent,
                   gint numberOfLines,
                   gboolean read_only )
{
    GNCSplitReg2 *gsrToRet;

    ENTER("ld=%p, parent=%p, numberOfLines=%d, read_only=%s",
          ld, parent, numberOfLines, read_only ? "TRUE" : "FALSE");

    gsrToRet = g_object_new (gnc_split_reg2_get_type(), NULL);

    gsrToRet->numRows = numberOfLines;
    gsrToRet->read_only = read_only;

    gsrToRet->ledger = ld;
    gsrToRet->window = GTK_WIDGET (parent);

    gnc_split_reg2_init2 (gsrToRet);

    LEAVE("%p", gsrToRet);
    return GTK_WIDGET (gsrToRet);
}

static void
gnc_split_reg2_init (GNCSplitReg2 *gsr)
{
    gsr->numRows = 10;
    gsr->read_only = FALSE;

    g_signal_connect (gsr, "destroy",
                      G_CALLBACK (gnc_split_reg2_destroy_cb), gsr );
}

static void
gnc_split_reg2_init2 (GNCSplitReg2 *gsr)
{
    if (!gsr) return;

    gnc_split_reg2_determine_read_only (gsr);

    gnc_split_reg2_determine_account_pr (gsr);

    gsr2_setup_status_widgets( gsr );
    /* ordering is important here... setup_status before create_table */

    gsr2_create_table( gsr );
    gsr2_setup_table( gsr );

}

static
void
gsr2_setup_table (GNCSplitReg2 *gsr)
{
    GncTreeModelSplitReg *model;

    ENTER("gsr=%p", gsr);

    model = gnc_ledger_display2_get_split_model_register (gsr->ledger);

    gnc_split_reg2_refresh_toolbar (gsr);

    LEAVE(" ");
}

static
void
gsr2_create_table (GNCSplitReg2 *gsr)
{
    GtkWidget *register_widget;
    GncTreeViewSplitReg *view;
    GncTreeModelSplitReg *model;
    GtkWidget *scrolled_window;
    GtkTreeViewColumn *col;
    GNCLedgerDisplay2Type ledger_type;

    gchar *gconf_key;
    const GncGUID * guid;
    Account * account;
    const gchar *gconf_section;
    const gchar *sort_string;
    
    account = gnc_ledger_display2_leader (gsr->ledger);
    guid = xaccAccountGetGUID (account);
    /* Used for saving different register column widths under seperate keys */
    gconf_key = g_strconcat (GCONF_SECTION,"/", (gchar*)guid_to_string (guid), NULL);

    ENTER("create table gsr=%p", gsr);

    gnc_ledger_display2_set_user_data (gsr->ledger, (gpointer)gsr);
    gnc_ledger_display2_set_handlers (gsr->ledger,
                                     gnc_split_reg2_ld_destroy,
                                     gnc_split_reg2_get_parent);

    ledger_type = gnc_ledger_display2_type (gsr->ledger);

    model = gnc_ledger_display2_get_split_model_register (gsr->ledger);
    view = gnc_tree_view_split_reg_new_with_model (model);

    g_object_unref (G_OBJECT (model));

    gnc_tree_model_split_reg_set_display (model, ((ledger_type == LD2_SUBACCOUNT)?TRUE:FALSE), ((ledger_type == LD2_GL)?TRUE:FALSE));

    // We need to give the General Ledger a Key other than all zeros which the search register gets.
//    if (account == NULL && model->type == GENERAL_LEDGER2)
    if (ledger_type == LD2_GL && model->type == GENERAL_LEDGER2)
        gconf_key = g_strconcat (GCONF_SECTION,"/", "00000000000000000000000000000001", NULL);

    scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_show (scrolled_window);
    gtk_box_pack_start (GTK_BOX (gsr), scrolled_window, TRUE, TRUE, 0);

    gnc_ledger_display2_set_split_view_register (gsr->ledger, view);

    /* Restore the sort depth from gconf */
    view->sort_depth = gnc_gconf_get_int (gconf_key, "sort_depth", NULL);

    /* Restore the sort order from gconf */
    sort_string = gnc_gconf_get_string (gconf_key, "sort_order", NULL);
    if (g_strcmp0 ("descending", sort_string) == 0)
        view->sort_direction = -1;
    else
        view->sort_direction = 1;

    g_object_set (G_OBJECT (view), "gconf-section", gconf_key, 
                 "show-column-menu", FALSE, NULL);

    gnc_tree_view_configure_columns (GNC_TREE_VIEW (view));

    if (ledger_type == LD2_GL && model->type == GENERAL_LEDGER2)
        gnc_tree_view_set_show_column_menu (GNC_TREE_VIEW (view), TRUE);
    else
        gnc_tree_view_set_show_column_menu (GNC_TREE_VIEW (view), FALSE);

    /* This column gets all the free space */
    gnc_tree_view_expand_columns (GNC_TREE_VIEW (view), "descnotes", NULL);

    /* This sets the status color column, 4 is the minimum */
    col = gnc_tree_view_find_column_by_name (GNC_TREE_VIEW (view), "status");
    if (col != NULL)
        g_object_set (G_OBJECT(col),
                     "resizable", FALSE,
                     "sizing", GTK_TREE_VIEW_COLUMN_FIXED,
                     "fixed-width", 4,
                      NULL);

    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view), TRUE);
    gtk_widget_show (GTK_WIDGET (view));

    gtk_container_add (GTK_CONTAINER (scrolled_window), GTK_WIDGET (view));
    gtk_widget_show (GTK_WIDGET (gsr));

    /* Should this be read only */
    gnc_tree_view_split_reg_set_read_only (view, gsr->read_only);

    /* This tells the ledger that we have a valid tree view */
    gnc_ledger_display2_set_split_view_refresh (gsr->ledger, TRUE);

    /* This triggers the update of the summary bar */
    g_signal_connect_after (model, "refresh_status_bar",
                      G_CALLBACK(gsr2_redraw_all_cb), gsr); //this works

    /* This triggers the update of the help text */
    g_signal_connect (view, "help_signal",
                      G_CALLBACK (gsr2_emit_help_changed), gsr); // this works

    LEAVE(" ");
}

static
void
gsr2_setup_status_widgets (GNCSplitReg2 *gsr)
{
    GncTreeModelSplitReg *model;
    gboolean use_double_line;

    model = gnc_ledger_display2_get_split_model_register (gsr->ledger);
    use_double_line = gnc_ledger_display2_default_double_line (gsr->ledger);

    /* be sure to initialize the gui elements */
    gnc_tree_model_split_reg_config (model, model->type, model->style, use_double_line);
}

void
gnc_split_reg2_destroy_cb (GtkWidget *widget, gpointer data)
{
}

/**
 * Jump to split.
 **/
void
gnc_split_reg2_jump_to_split (GNCSplitReg2 *gsr, Split *split)
{
    GncTreeViewSplitReg *view;

    if (gsr == NULL)
        return;

    if (split == NULL)
        return;

    view = gnc_ledger_display2_get_split_view_register (gsr->ledger);

    gnc_tree_control_split_reg_jump_to (view, NULL, split, FALSE);
}


/**
 * Move the cursor to the split in the non-blank amount column.
 **/
void
gnc_split_reg2_jump_to_split_amount (GNCSplitReg2 *gsr, Split *split)
{
    GncTreeViewSplitReg *view;

    if (gsr == NULL)
        return;

    if (split == NULL)
        return;

    view = gnc_ledger_display2_get_split_view_register (gsr->ledger);

    gnc_tree_control_split_reg_jump_to (view, NULL, split, TRUE);
}

/**
 * Raise an existing register window to the front.
 **/
void
gnc_split_reg2_raise (GNCSplitReg2 *gsr)
{
    if (gsr == NULL)
        return;

    if (gsr->window == NULL)
        return;

    gtk_window_present (GTK_WINDOW (gsr->window));
}


/**
 * Duplicate-code reduction function; retreives, formats and updates the
 * GtkLabel with the given amount.
 **/
static
void
gsr2_update_summary_label (GtkWidget *label,
                          xaccGetBalanceFn getter,
                          Account *leader,
                          GNCPrintAmountInfo print_info,
                          gnc_commodity *cmdty,
                          gboolean reverse,
                          gboolean euroFlag)
{
    gnc_numeric amount;
    char string[256];

    if ( label == NULL )
        return;

    amount = (*getter)( leader );

    if ( reverse )
    {
        amount = gnc_numeric_neg( amount );
    }

    xaccSPrintAmount( string, amount, print_info );

    if ( euroFlag )
    {
        strcat( string, " / " );
        xaccSPrintAmount( string + strlen( string ),
                          gnc_convert_to_euro( cmdty, amount ),
                          gnc_commodity_print_info( gnc_get_euro(), TRUE ) );
    }

    gnc_set_label_color( label, amount );
    gtk_label_set_text( GTK_LABEL(label), string );
}

static GNCPrice *
account_latest_price (Account *account)
{
    QofBook *book;
    GNCPriceDB *pdb;
    gnc_commodity *commodity;
    gnc_commodity *currency;

    if (!account) return NULL;
    commodity = xaccAccountGetCommodity (account);
    currency = gnc_default_currency ();

    book = gnc_account_get_book (account);
    pdb = gnc_pricedb_get_db (book);

    return gnc_pricedb_lookup_latest (pdb, commodity, currency);
}

static GNCPrice *
account_latest_price_any_currency (Account *account)
{
    QofBook *book;
    GNCPriceDB *pdb;
    gnc_commodity *commodity;
    GList *price_list;
    GNCPrice *result;

    if (!account) return NULL;
    commodity = xaccAccountGetCommodity (account);

    book = gnc_account_get_book (account);
    pdb = gnc_pricedb_get_db (book);

    price_list = gnc_pricedb_lookup_latest_any_currency (pdb, commodity);
    if (!price_list) return NULL;

    result = gnc_price_clone ((GNCPrice *)(price_list->data), book);

    gnc_price_list_destroy (price_list);

    return result;
}

static
void
gsr2_redraw_all_cb (GncTreeViewSplitReg *view, gpointer user_data)
{
    GNCSplitReg2 *gsr = user_data;
    gnc_commodity * commodity;
    GNCPrintAmountInfo print_info;
    gnc_numeric amount;
    char string[256];
    Account *leader;
    gboolean reverse;
    gboolean euro;

    if ( gsr->summarybar == NULL )
        return;

    leader = gnc_ledger_display2_leader( gsr->ledger );

    commodity = xaccAccountGetCommodity( leader );

    /* no EURO converson, if account is already EURO or no EURO currency */
    if (commodity != NULL)
        euro = (gnc_is_euro_currency( commodity ) &&
                (strncasecmp(gnc_commodity_get_mnemonic(commodity), "EUR", 3)));
    else
        euro = FALSE;

    print_info = gnc_account_print_info( leader, TRUE );
    reverse = gnc_reverse_balance( leader );

    gsr2_update_summary_label( gsr->balance_label,
                              xaccAccountGetPresentBalance,
                              leader, print_info, commodity, reverse, euro );
    gsr2_update_summary_label( gsr->cleared_label,
                              xaccAccountGetClearedBalance,
                              leader, print_info, commodity, reverse, euro );
    gsr2_update_summary_label( gsr->reconciled_label,
                              xaccAccountGetReconciledBalance,
                              leader, print_info, commodity, reverse, euro );
    gsr2_update_summary_label( gsr->future_label,
                              xaccAccountGetBalance,
                              leader, print_info, commodity, reverse, euro );
    gsr2_update_summary_label( gsr->projectedminimum_label,
                              xaccAccountGetProjectedMinimumBalance,
                              leader, print_info, commodity, reverse, euro );

    /* Print the summary share amount */
    if (gsr->shares_label != NULL)
    {
        print_info = gnc_account_print_info( leader, TRUE );

        amount = xaccAccountGetBalance( leader );
        if (reverse)
            amount = gnc_numeric_neg( amount );

        xaccSPrintAmount( string, amount, print_info );

        gnc_set_label_color( gsr->shares_label, amount );
        gtk_label_set_text( GTK_LABEL(gsr->shares_label), string );
    }

    /* Print the summary share value */
    if (gsr->value_label != NULL)
    {
        GNCPrice *price;

        amount = xaccAccountGetBalance (leader);
        if (reverse) amount = gnc_numeric_neg (amount);

        price = account_latest_price (leader);
        if (!price)
        {
            /* If the balance is zero, then print zero. */
            if (gnc_numeric_equal(amount, gnc_numeric_zero()))
            {
                gnc_commodity *currency = gnc_default_currency ();
                print_info = gnc_commodity_print_info (currency, TRUE);
                amount = gnc_numeric_zero ();

                xaccSPrintAmount (string, amount, print_info);

                gnc_set_label_color (gsr->value_label, amount);
                gtk_label_set_text (GTK_LABEL (gsr->value_label), string);
            }
            else
            {
                /* else try to do a double-price-conversion :-( */
                price = account_latest_price_any_currency (leader);
                if (!price)
                {
                    gnc_set_label_color (gsr->value_label, gnc_numeric_zero ());
                    gtk_label_set_text (GTK_LABEL (gsr->value_label),
                                        _("<No information>"));
                }
                else
                {
                    gnc_commodity *currency = gnc_price_get_currency (price);
                    gnc_commodity *default_currency = gnc_default_currency ();
                    gnc_numeric currency_amount;
                    gnc_numeric default_currency_amount;

                    print_info = gnc_commodity_print_info (currency, TRUE);

                    currency_amount =
                        xaccAccountConvertBalanceToCurrency(leader, amount,
                                                            commodity, currency);
                    xaccSPrintAmount (string, currency_amount, print_info);

                    default_currency_amount =
                        xaccAccountConvertBalanceToCurrency(leader, amount,
                                                            commodity,
                                                            default_currency);
                    if (!gnc_numeric_zero_p(default_currency_amount))
                    {
                        strcat( string, " / " );
                        print_info = gnc_commodity_print_info (default_currency, TRUE);
                        xaccSPrintAmount( string + strlen( string ), default_currency_amount,
                                          print_info);
                    }

                    gnc_set_label_color (gsr->value_label, amount);
                    gtk_label_set_text (GTK_LABEL (gsr->value_label), string);

                    gnc_price_unref (price);
                }
            }
        }
        else
        {
            gnc_commodity *currency = gnc_price_get_currency (price);

            print_info = gnc_commodity_print_info (currency, TRUE);

            amount = gnc_numeric_mul (amount, gnc_price_get_value (price),
                                      gnc_commodity_get_fraction (currency),
                                      GNC_HOW_RND_ROUND_HALF_UP);

            xaccSPrintAmount (string, amount, print_info);

            gnc_set_label_color (gsr->value_label, amount);
            gtk_label_set_text (GTK_LABEL (gsr->value_label), string);

            gnc_price_unref (price);
        }
    }
}

static void
gnc_split_reg2_refresh_toolbar (GNCSplitReg2 *gsr)
{
    GtkToolbarStyle tbstyle;

    if ((gsr == NULL) || (gsr->toolbar == NULL))
        return;

    tbstyle = gnc_get_toolbar_style ();
    gtk_toolbar_set_style (GTK_TOOLBAR (gsr->toolbar), tbstyle );
}

static void
gnc_split_reg2_ld_destroy (GNCLedgerDisplay2 *ledger)
{
    GNCSplitReg2 *gsr = gnc_ledger_display2_get_user_data (ledger);
    
    gchar *gconf_key;
    const GncGUID * guid;
    Account * account;
    
    account = gnc_ledger_display2_leader (ledger);
    guid = xaccAccountGetGUID (account);
    gconf_key = (gchar*)guid_to_string (guid);
    
    if (gsr)
    {
        GncTreeModelSplitReg *model;

        model = gnc_ledger_display2_get_split_model_register (ledger);

/*FIXME This may not be required
        if (model && model->table)
            gnc_table_save_state (model->table, gconf_key);
*/
        /*
         * Don't destroy the window here any more.  The register no longer
         * owns it.
         */
    }
    gnc_ledger_display2_set_user_data (ledger, NULL);
}


/* ########################### Handlers ############################### */

void
gnc_split_reg2_balancing_entry (GNCSplitReg2 *gsr, Account *account,
                              time64 statement_date, gnc_numeric balancing_amount) // this works
{
    GncTreeViewSplitReg *view;
    Transaction *transaction;
    Split *split;

    view = gnc_ledger_display2_get_split_view_register (gsr->ledger);

    // create transaction
    transaction = gsr2_create_balancing_transaction (gnc_get_current_book(),
                  account, statement_date, balancing_amount);

    // jump to transaction
    split = xaccTransFindSplitByAccount (transaction, account);
    if (split == NULL)
    {
        // default behaviour: jump to blank split
        g_warning("gsr2_create_balancing_transaction failed");
        gnc_tree_control_split_reg_jump_to_blank (view);
    }
    else
    {
        // goto balancing transaction
        gnc_tree_control_split_reg_jump_to (view, NULL, split, FALSE);
    }
}

static Transaction*
gsr2_create_balancing_transaction (QofBook *book, Account *account,
                             time64 statement_date, gnc_numeric balancing_amount)
{
    Transaction *trans;
    Split *split;

    if (!account)
        return NULL;
    if (gnc_numeric_zero_p (balancing_amount))
        return NULL;

    xaccAccountBeginEdit (account);

    trans = xaccMallocTransaction (book);

    xaccTransBeginEdit (trans);

    // fill Transaction
    xaccTransSetCurrency (trans, xaccAccountGetCommodity (account));
    xaccTransSetDatePostedSecs (trans, statement_date);
    xaccTransSetDescription (trans, _("Balancing entry from reconcilation"));

    // 1. Split
    split = xaccMallocSplit (book);
    xaccTransAppendSplit (trans, split);
    xaccAccountInsertSplit  (account, split);
    xaccSplitSetAmount (split, balancing_amount);
    xaccSplitSetValue (split, balancing_amount);

    // 2. Split (no account is defined: split goes to orphan account)
    split = xaccMallocSplit (book);
    xaccTransAppendSplit (trans, split);

    balancing_amount = gnc_numeric_neg (balancing_amount);
    xaccSplitSetAmount (split, balancing_amount);
    xaccSplitSetValue (split, balancing_amount);

    xaccTransCommitEdit (trans);
    xaccAccountCommitEdit (account);
    return trans;
}

/* ############################## End Handlers ############################ */

void
gnc_split_reg2_change_style (GNCSplitReg2 *gsr, SplitRegisterStyle2 style)
{
    GncTreeModelSplitReg *model = gnc_ledger_display2_get_split_model_register (gsr->ledger);

    if (style == model->style)
        return;

    gnc_tree_model_split_reg_config (model, model->type, style, model->use_double_line);

    // This will re-display the view.
    gnc_tree_view_split_reg_set_format (gnc_ledger_display2_get_split_view_register (gsr->ledger));
}

void
gnc_split_reg2_style_ledger_cb (GtkWidget *w, gpointer data)
{
    GNCSplitReg2 *gsr = data;

    if (!GTK_CHECK_MENU_ITEM (w)->active)
        return;

    gnc_split_reg2_change_style (gsr, REG2_STYLE_LEDGER);
}

void
gnc_split_reg2_style_auto_ledger_cb (GtkWidget *w, gpointer data)
{
    GNCSplitReg2 *gsr = data;

    if (!GTK_CHECK_MENU_ITEM (w)->active)
        return;

    gnc_split_reg2_change_style (gsr, REG2_STYLE_AUTO_LEDGER);
}

void
gnc_split_reg2_style_journal_cb (GtkWidget *w, gpointer data)
{
    GNCSplitReg2 *gsr = data;

    if (!GTK_CHECK_MENU_ITEM (w)->active)
        return;

    gnc_split_reg2_change_style (gsr, REG2_STYLE_JOURNAL);
}

void
gnc_split_reg2_double_line_cb (GtkWidget *w, gpointer data)
{
    GNCSplitReg2 *gsr = data;
    GncTreeModelSplitReg *model = gnc_ledger_display2_get_split_model_register (gsr->ledger);
    gboolean use_double_line;

    use_double_line = GTK_CHECK_MENU_ITEM (w)->active;
    if (use_double_line == model->use_double_line)
        return;

    gnc_tree_model_split_reg_config (model, model->type, model->style, use_double_line);

    // This will re-display the view.
    gnc_tree_view_split_reg_set_format (gnc_ledger_display2_get_split_view_register (gsr->ledger));
}

static
GtkWidget*
add_summary_label (GtkWidget *summarybar, const char *label_str)
{
    GtkWidget *hbox;
    GtkWidget *label;

    hbox = gtk_hbox_new (FALSE, 2);
    gtk_box_pack_start (GTK_BOX (summarybar), hbox, FALSE, FALSE, 5);

    label = gtk_label_new (label_str);
    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    label = gtk_label_new ("");
    gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
    gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    return label;
}

GtkWidget *
gnc_split_reg2_create_summary_bar (GNCSplitReg2 *gsr)
{
    GtkWidget *summarybar;

    gsr->cleared_label    = NULL;
    gsr->balance_label    = NULL;
    gsr->reconciled_label = NULL;
    gsr->future_label     = NULL;
    gsr->projectedminimum_label = NULL;
    gsr->shares_label     = NULL;
    gsr->value_label      = NULL;

    if (gnc_ledger_display2_type (gsr->ledger) >= LD2_SUBACCOUNT)
    {
        gsr->summarybar = NULL;
        return NULL;
    }

    summarybar = gtk_hbox_new (FALSE, 4);

    if (!xaccAccountIsPriced(gnc_ledger_display2_leader(gsr->ledger)))
    {
        gsr->balance_label    = add_summary_label (summarybar, _("Present:"));
        gsr->future_label     = add_summary_label (summarybar, _("Future:"));
        gsr->cleared_label    = add_summary_label (summarybar, _("Cleared:"));
        gsr->reconciled_label = add_summary_label (summarybar, _("Reconciled:"));
        gsr->projectedminimum_label  = add_summary_label (summarybar, _("Projected Minimum:"));
    }
    else
    {
        gsr->shares_label     = add_summary_label (summarybar, _("Shares:"));
        gsr->value_label      = add_summary_label (summarybar, _("Current Value:"));
    }

    gsr->summarybar = summarybar;

    /* Force the first update */
    gsr2_redraw_all_cb (NULL, gsr);
    return gsr->summarybar;
}

/**
 * Opens up a register window for a group of Accounts.
 * @param gsr the register window instance
 * @return A GNCPlaceholderType indicating presence and type of placeholder
 * accounts
 **/
static
GNCPlaceholderType
gnc_split_reg2_get_placeholder (GNCSplitReg2 *gsr)
{
    Account *leader;
    GncTreeModelSplitReg *model;
    gboolean single_account;

    if (gsr == NULL)
        return PLACEHOLDER_NONE;

    model = gnc_ledger_display2_get_split_model_register (gsr->ledger);

    switch (model->type)
    {
    case GENERAL_LEDGER2:
    case INCOME_LEDGER2:
    case PORTFOLIO_LEDGER2:
    case SEARCH_LEDGER2:
        single_account = FALSE;
        break;
    default:
        single_account = TRUE;
        break;
    }

    leader = gnc_ledger_display2_leader (gsr->ledger);

    if (leader == NULL)
        return PLACEHOLDER_NONE;
    if (single_account)
    {
        if (xaccAccountGetPlaceholder (leader))
            return PLACEHOLDER_THIS;
        return PLACEHOLDER_NONE;
    }
    return xaccAccountGetDescendantPlaceholder (leader);
}



/**
 * @see gtk_callback_bug_workaround
 **/
typedef struct dialog_args
{
    GNCSplitReg2 *gsr;
    gchar *string;
} dialog_args;


/* This Register is an Account Payable / Receivable one */
static
gboolean
gsr2_determine_account_pr_dialog (gpointer argp)
{
    dialog_args *args = argp;
    GtkWidget *dialog;

    const char *title = _("Account Payable / Receivable Register");
    const char *message =
            _("The register displayed is for Account Payable or Account Receivable. "
              "Changing the entries may cause harm, please use the business "
              "options to change the entries.");

    dialog = gtk_message_dialog_new (GTK_WINDOW (args->gsr->window),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_WARNING,
                                     GTK_BUTTONS_CLOSE,
                                     "%s", title);
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                              "%s", message);

    gnc_dialog_run (GTK_DIALOG (dialog), "accounts_payable_receivable");
    gtk_widget_destroy (dialog);
    g_free (args);
    return FALSE;
}


/* This Register is an Account Payable / Recievable one */
static void
gnc_split_reg2_determine_account_pr (GNCSplitReg2 *gsr)
{
    dialog_args *args = g_malloc (sizeof (dialog_args));
    GncTreeModelSplitReg *model;

    model = gnc_ledger_display2_get_split_model_register (gsr->ledger);

    if (model->type != PAYABLE_REGISTER2 && model->type != RECEIVABLE_REGISTER2)
        return;

    /* Put up a warning dialog */
    args->gsr = gsr;
    g_timeout_add (250, gsr2_determine_account_pr_dialog, args); /* 0.25 seconds */
}


/**
 * Gtk has occasional problems with performing function as part of a
 * callback.  This routine gets called via a timer callback to get it out of
 * the data path with the problem.
 **/
static
gboolean
gtk_callback_bug_workaround (gpointer argp)
{
    dialog_args *args = argp;
    const gchar *read_only = _("This account register is read-only.");
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new (GTK_WINDOW(args->gsr->window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_WARNING,
                                    GTK_BUTTONS_CLOSE,
                                    "%s", read_only);
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
            "%s", args->string);
    gnc_dialog_run (GTK_DIALOG (dialog), "register_read_only");
    gtk_widget_destroy (dialog);
    g_free (args);
    return FALSE;
}

/**
 * Determines whether this register window should be read-only.
 **/
static
void
gnc_split_reg2_determine_read_only (GNCSplitReg2 *gsr) //this works
{
    dialog_args *args = g_malloc (sizeof (dialog_args));
    GncTreeViewSplitReg *view;

    if (qof_book_is_readonly (gnc_get_current_book()))
    {
        /* Is the book read-only? Then for sure also make this register
        read-only. */
        gsr->read_only = TRUE;
    }

    if (!gsr->read_only)
    {

        switch (gnc_split_reg2_get_placeholder (gsr))
        {
        case PLACEHOLDER_NONE:
            /* stay as false. */
            return;

        case PLACEHOLDER_THIS:
            args->string = _("This account may not be edited.  If you want "
                             "to edit transactions in this register, please "
                             "open the account options and turn off the "
                             "placeholder checkbox.");
            break;

        default:
            args->string = _("One of the sub-accounts selected may not be "
                             "edited.  If you want to edit transactions in "
                             "this register, please open the sub-account "
                             "options and turn off the placeholder checkbox. "
                             "You may also open an individual account instead "
                             "of a set of accounts.");
            break;
        }
        gsr->read_only = TRUE;
        /* Put up a warning dialog */
        args->gsr = gsr;
        g_timeout_add (250, gtk_callback_bug_workaround, args); /* 0.25 seconds */
    }
}

static
GtkWidget *
gnc_split_reg2_get_parent (GNCLedgerDisplay2 *ledger)
{
    GNCSplitReg2 *gsr =
        GNC_SPLIT_REG2 (gnc_ledger_display2_get_user_data (ledger));

    if (gsr == NULL)
        return NULL;

    return gsr->window;
}

static
void
gsr2_emit_help_changed (GncTreeViewSplitReg *view, gpointer user_data) //this works
{
    gsr2_emit_simple_signal ((GNCSplitReg2*)user_data, "help-changed" );
}

static
void
gsr2_emit_simple_signal (GNCSplitReg2 *gsr, const char *sigName) //this works
{
    g_signal_emit_by_name( gsr, sigName, NULL );
}

GncTreeViewSplitReg *
gnc_split_reg2_get_register (GNCSplitReg2 *gsr )
{
    if ( !gsr )
        return NULL;

    return gnc_ledger_display2_get_split_view_register (gsr->ledger);
}

GtkWidget*
gnc_split_reg2_get_summarybar (GNCSplitReg2 *gsr)
{
    if (!gsr) return NULL;
    return gsr->summarybar;
}

gboolean
gnc_split_reg2_get_read_only (GNCSplitReg2 *gsr)
{
    g_assert (gsr);
    return gsr->read_only;
}

void
gnc_split_reg2_set_moved_cb (GNCSplitReg2 *gsr, GFunc cb, gpointer cb_data ) //this works
{
    gnc_tree_view_split_reg_moved_cb (gnc_ledger_display2_get_split_view_register (gsr->ledger), cb, cb_data);
}
