// FoldersPage.cpp

#include "StdAfx.h"

#include "FoldersPageRes.h"
#include "FoldersPage.h"

#include "../FileManager/BrowseDialog.h"
#include "../FileManager/HelpUtils.h"
#include "../FileManager/LangUtils.h"

using namespace NWindows;

static const UInt32 kLangIDs[] =
{
  IDT_FOLDERS_WORKING_FOLDER,
  IDR_FOLDERS_WORK_SYSTEM,
  IDR_FOLDERS_WORK_CURRENT,
  IDR_FOLDERS_WORK_SPECIFIED,
  IDX_FOLDERS_WORK_FOR_REMOVABLE
};

static const int kWorkModeButtons[] =
{
  IDR_FOLDERS_WORK_SYSTEM,
  IDR_FOLDERS_WORK_CURRENT,
  IDR_FOLDERS_WORK_SPECIFIED
};

static const int kNumWorkModeButtons = ARRAY_SIZE(kWorkModeButtons);
 
bool CFoldersPage::OnInit()
{
  LangSetDlgItems(*this, kLangIDs, ARRAY_SIZE(kLangIDs));
  m_WorkDirInfo.Load();

  CheckButton(IDX_FOLDERS_WORK_FOR_REMOVABLE, m_WorkDirInfo.ForRemovableOnly);
  
  CheckRadioButton(kWorkModeButtons[0], kWorkModeButtons[kNumWorkModeButtons - 1],
      kWorkModeButtons[m_WorkDirInfo.Mode]);

  m_WorkPath.Init(*this, IDE_FOLDERS_WORK_PATH);
  m_ButtonSetWorkPath.Init(*this, IDB_FOLDERS_WORK_PATH);

  m_WorkPath.SetText(fs2us(m_WorkDirInfo.Path));

  MyEnableControls();
  
  return CPropertyPage::OnInit();
}

int CFoldersPage::GetWorkMode() const
{
  for (int i = 0; i < kNumWorkModeButtons; i++)
    if (IsButtonCheckedBool(kWorkModeButtons[i]))
      return i;
  throw 0;
}

void CFoldersPage::MyEnableControls()
{
  bool enablePath = (GetWorkMode() == NWorkDir::NMode::kSpecified);
  m_WorkPath.Enable(enablePath);
  m_ButtonSetWorkPath.Enable(enablePath);
}

void CFoldersPage::GetWorkDir(NWorkDir::CInfo &workDirInfo)
{
  UString s;
  m_WorkPath.GetText(s);
  workDirInfo.Path = us2fs(s);
  workDirInfo.ForRemovableOnly = IsButtonCheckedBool(IDX_FOLDERS_WORK_FOR_REMOVABLE);
  workDirInfo.Mode = NWorkDir::NMode::EEnum(GetWorkMode());
}

/*
bool CFoldersPage::WasChanged()
{
  NWorkDir::CInfo workDirInfo;
  GetWorkDir(workDirInfo);
  return (workDirInfo.Mode != m_WorkDirInfo.Mode ||
      workDirInfo.ForRemovableOnly != m_WorkDirInfo.ForRemovableOnly ||
      workDirInfo.Path.Compare(m_WorkDirInfo.Path) != 0);
}
*/

void CFoldersPage::ModifiedEvent()
{
  Changed();
  /*
  if (WasChanged())
    Changed();
  else
    UnChanged();
  */
}

bool CFoldersPage::OnButtonClicked(int buttonID, HWND buttonHWND)
{
  for (int i = 0; i < kNumWorkModeButtons; i++)
    if (buttonID == kWorkModeButtons[i])
    {
      MyEnableControls();
      ModifiedEvent();
      return true;
    }
  switch (buttonID)
  {
    case IDB_FOLDERS_WORK_PATH:
      OnFoldersWorkButtonPath();
      break;
    case IDX_FOLDERS_WORK_FOR_REMOVABLE:
      break;
    default:
      return CPropertyPage::OnButtonClicked(buttonID, buttonHWND);
  }
  ModifiedEvent();
  return true;
}

bool CFoldersPage::OnCommand(int code, int itemID, LPARAM lParam)
{
  if (code == EN_CHANGE && itemID == IDE_FOLDERS_WORK_PATH)
  {
    ModifiedEvent();
    return true;
  }
  return CPropertyPage::OnCommand(code, itemID, lParam);
}

void CFoldersPage::OnFoldersWorkButtonPath()
{
  UString currentPath;
  m_WorkPath.GetText(currentPath);
  UString title = LangString(IDS_FOLDERS_SET_WORK_PATH_TITLE);
  UString resultPath;
  if (MyBrowseForFolder(*this, title, currentPath, resultPath))
    m_WorkPath.SetText(resultPath);
}

LONG CFoldersPage::OnApply()
{
  GetWorkDir(m_WorkDirInfo);
  m_WorkDirInfo.Save();
  return PSNRET_NOERROR;
}

static LPCWSTR kFoldersTopic = L"fm/options.htm#folders";

void CFoldersPage::OnNotifyHelp()
{
  ShowHelpWindow(NULL, kFoldersTopic);
}
